// sendmmsg needs the GNU Source define
// This needs to be at the top or sendmmsg will not be found.
#define _GNU_SOURCE 1

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#undef _GNU_SOURCE

// PSRDADA includes
#include "ilt_dada.h"

#define PACKET_SIZE (UDPHDRLEN + UDPNPOL * UDPNTIMESLICE * 122)

void helpMessages() {
	printf("ILTDada fillbuffer (CLI v%s, lib %s)\n\n", ILTD_CLI_VERSION, ILTD_VERSION);

	printf("-h				: Display this message\n");
	printf("-k (int)		: Target DADA buffer (default, output to ringbuffer at %d)\n", DEF_PORT);
	printf("-u (int,int)	: Target port and offset between ports (default, e.g., %d,1)\n\t\tIf this option is set to 0, data will be written directly to the ringbuffer set by '-k'\n", DEF_PORT);
	printf("-H (str)		: Target machine IP (e.g., localhost, my.server.com)\n");
	printf("-i (str)		: Input raw data file\n");
	printf("-p (int)		: Packets loaded and sent per operation (default: 1024)\n");
	printf("-n (int)		: Number of target ports (default: 1)\n");
	printf("-t (int)		: Total number of pakcets to loadand send (default: entire input)\n");
	printf("-w (int)		: Time in milliseconds to wait between starting operations (default: 1) (upper limit on throughput, may not be reached if disks / CPU are too slow)\n\n");
}

int main(int argc, char *argv[]) {

	int inputOpt, packets = 1, waitTime = 1;
	char inputFile[DEF_STR_LEN] = "", workingName[DEF_STR_LEN] = "", hostIP[DEF_STR_LEN] = "127.0.0.1";
	long totalPackets = LONG_MAX, packetCount = 0, writtenBytes;
	int numPorts = 1;
	int offset = 10, fullReads = 1, portOffset = 1;

	FILE *inputFiles[MAX_NUM_PORTS];

	ilt_dada_config *config[MAX_NUM_PORTS];
	config[0] = ilt_dada_init();


	config[0]->portNum = DEF_PORT;
	config[0]->packetsPerIteration = 1024;
	config[0]->io->outputDadaKeys[0] = DEF_PORT;
	config[0]->recvflags = -1;

	while((inputOpt = getopt(argc, argv, "u:H:i:p:n:k:t:w:h")) != -1) {
		switch(inputOpt) {

			case 'u':
				sscanf(optarg, "%d,%d", &(config[0]->portNum), &portOffset);
				if (config[0]->portNum == 0) {
					packets = 0;
				}
				break;

			case 'H':
				strcpy(&(hostIP[0]), optarg);
				break;
			
			case 'i':
				strcpy(inputFile, optarg);
				break;

			case 'p':
				config[0]->packetsPerIteration = atoi(optarg);
				break;

			case 'n':
				numPorts = atoi(optarg);

				if (numPorts > MAX_NUM_PORTS) {
					fprintf(stderr, "ERROR: You requested %d ports, but LOFAR can only produce %d. Exiting.\n", numPorts, MAX_NUM_PORTS);
					return 1;
				}
				break;

			case 'k':
				sscanf(optarg, "%d,%d", &(config[0]->io->outputDadaKeys[0]), &offset);
				break;

			case 't':
				totalPackets = atol(optarg);
				break;

			case 'w':
				waitTime = atoi(optarg);
				break;

			case 'h':
				helpMessages();
				return 0;

			default:
				fprintf(stderr, "ERROR: Unknown input %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	printf("Preparing to load data from %d file(s) following format %s, with %d packets per iteration.\n", numPorts, inputFile, config[0]->packetsPerIteration);

	if (packets) {
		printf("We will be using UDP packets to copy the data starting on host/port %s:%d with an offset of 1. \n", hostIP, config[0]->portNum);
	} else {
		printf("We will be copying the data into the ringbuffers starting at %d (%x) with an offset of %d by copying data directly to the ringbuffer.\n\n", config[0]->io->outputDadaKeys[0], config[0]->io->outputDadaKeys[0], offset);
	}


	config[0]->portBufferSize = 4 * PACKET_SIZE * config[0]->packetsPerIteration;
	config[0]->io->numOutputs = numPorts;
	for (int port = 0; port < numPorts; port++) {
		if (port != 0) {
			config[port] = ilt_dada_init();
		}
		config[port]->portNum = config[0]->portNum + port * portOffset;

		config[port]->packetsPerIteration = config[0]->packetsPerIteration;
		config[port]->portBufferSize = config[0]->portBufferSize;
		config[port]->recvflags = config[0]->recvflags;


		if (packets == 0) {
			config[0]->io->readerType = DADA_ACTIVE;
			config[0]->io->dadaConfig.nbufs = 32;
			config[0]->io->writeBufSize[port] = 4 * PACKET_SIZE * config[0]->packetsPerIteration;
			config[0]->io->outputDadaKeys[port] = config[0]->io->outputDadaKeys[0] + offset * port;
		}
	}
	if (packets == 0) {
		// Initialise the ringbuffer, exit on failure
		if (lofar_udp_io_write_setup(config[0]->io, 0) < 0) {
			return 1;
		}
	}

	struct addrinfo *serverInfo;

	if (packets == 1) {
		printf("Initialisng networking components...\n");
		for (int port = 0; port < numPorts; port++) {

			if (ilt_dada_initialise_port(config[port]) < 0) {
				return 1;
			}
		}

		const struct addrinfo addressInfo = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
			.ai_flags = IPPROTO_UDP
		};

		// Convert the port to a string for getaddrinfo
		char portNumStr[16];
		sprintf(portNumStr, "%d", config[0]->portNum);

		// Struct to collect the results from getaddrinfo
		int status;

		// Populate the remaining parts of addressInfo
		if ((status = getaddrinfo(hostIP, portNumStr, &addressInfo, &serverInfo)) < 0) {
			fprintf(stderr, "ERROR: Failed to get address info on port %d (errno %d: %s).", config[0]->portNum, status, gai_strerror(status));
			return 1;
		}

	}


	for (int port = 0; port < numPorts; port++) {
		sprintf(workingName, inputFile, port);

		printf("Opening file at %s...\n", workingName);
		inputFiles[port] = fopen(workingName, "r");
		if (inputFiles[port] == NULL) {
			fprintf(stderr, "Input file at %s does not exist, exiting.\n", workingName);
			return 1;
		}

		
		if (packets == 0) {
			printf("Allocating %ld MB for %d %d byte packets on port %d\n", (long) config[0]->packetsPerIteration * PACKET_SIZE >> 20, config[0]->packetsPerIteration, PACKET_SIZE, port);
			config[port]->params->packetBuffer = calloc(config[0]->packetsPerIteration * PACKET_SIZE, sizeof(int8_t));

			if (config[port]->params->packetBuffer == NULL) {
				fprintf(stderr, "ERROR: Failed to allocate data on port %d, exiting.\n", port);
				return 1;
			}

		} else {
			if (ilt_data_operate_prepare(config[port]) < 0) {
				return 1;
			}

			if (connect(config[port]->sockfd, serverInfo->ai_addr, sizeof(struct sockaddr)) == -1) {
				fprintf(stderr, "ERROR: Unable to connect to remote host %s:%d (errno %d, %s)\n", hostIP, config[port]->portNum, errno, strerror(errno));
				return 1;
			}

		}
	}



	while (packetCount < totalPackets && fullReads) {
		for (int port = 0; port < numPorts; port++) {
			const size_t readBytes = fread(&(config[port]->params->packetBuffer[0]), sizeof(char),
			                       config[port]->packetsPerIteration * PACKET_SIZE, inputFiles[port]);

			if (packets == 0) {
				writtenBytes = lofar_udp_io_write(config[0]->io, port, (int8_t*) config[port]->params->packetBuffer, readBytes);
			} else {
				writtenBytes = sendmmsg(config[port]->sockfd, config[port]->params->msgvec, config[port]->packetsPerIteration, 0);
				// sendmmg returns number of packets, multiply by packet length to get bytes
				writtenBytes *= PACKET_SIZE;
			}

			if (writtenBytes < 0) {
				fprintf(stderr, "ERROR: Failed to write data, exiting.\n");
				break;
			}

			if (readBytes != (size_t) (config[port]->packetsPerIteration * PACKET_SIZE)) {
				fullReads = 0;
				printf("Read less data than expected (%ld, %ld), likely reached EOF, finishing up.\n", readBytes, (long) config[port]->packetsPerIteration * PACKET_SIZE);
			}


			if (writtenBytes != (long) readBytes) {
				fprintf(stderr, "WARNING Port %d: Tried to send/write %ld bytes to buffer but only wrote %ld (errno: %d, %s).\n", port, readBytes, writtenBytes, errno, strerror(errno));
				if (packets && writtenBytes == PACKET_SIZE) {
					fprintf(stderr, "Is there no listener on the other side of your UDP port?\n");
				}
			} else {
				printf("Port %d: sent/wrote %d packets to buffer.\n", port, config[port]->packetsPerIteration);
			}
		}
		printf("\n\n");
		usleep(1000 * waitTime);
	}


	for (int port = 0; port < numPorts; port++) {
		printf("Freeing memory/closing file for port %d\n", port);

		ilt_dada_config_cleanup(config[port]);
		fclose(inputFiles[port]);
	}

	cleanup_initialise_port(serverInfo, -1);

}
