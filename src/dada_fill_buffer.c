// sendmmsg needs the GNU Source define
// This needs to be at the top or sendmmsg will not be found.
#define _GNU_SOURCE

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

// UPM defines are needed
#include "lofar_udp_general.h"

// PSRDADA includes
#include "ilt_dada.h"

#define PACKET_SIZE (UDPHDRLEN + UDPNPOL * UDPNTIMESLICE * 122)

int main(int argc, char *argv[]) {

	int inputOpt, packets = 0, dadaInput, waitTime = 1;
	char inputFile[2048] = "", workingName[2048] = "", hostname[2048] = "";
	long totalPackets = LONG_MAX, packetCount = 0, writtenBytes, datums;
	int numPorts = 1;
	int offset = 10, port = 16130, fullReads = 1, portOffset = 1;

	char* rawData[MAX_NUM_PORTS];
	FILE *inputFiles[MAX_NUM_PORTS];
	ipcio_t ringbuffer[MAX_NUM_PORTS];
	ipcio_t header[MAX_NUM_PORTS];

	ilt_dada_config config[MAX_NUM_PORTS] = { ilt_dada_config_default };
	ilt_dada_operate_params params[MAX_NUM_PORTS] = { ilt_dada_operate_params_default };

	while((inputOpt = getopt(argc, argv, "ui:p:n:k:t:w:")) != -1) {
		switch(inputOpt) {

			case 'u':
				packets = 1;
				sscanf(optarg, "%s:%d,%d", config[0].hostname, &(config[0].portNum), &portOffset);
				break;
			
			case 'i':
				strcpy(inputFile, optarg);
				break;

			case 'p':
				config[0].packetsPerIteration = atoi(optarg);
				break;

			case 'n':
				numPorts = atoi(optarg);

				if (numPorts > MAX_NUM_PORTS) {
					fprintf(stderr, "ERROR: You requested %d ports, but LOFAR can only produce %d. Exiting.\n", numPorts, MAX_NUM_PORTS);
					return 1;
				}
				break;

			case 'k':
				dadaInput = sscanf(optarg, "%d,%d", &(config[0].key), &offset);
				break;

			case 't':
				totalPackets = atol(optarg);
				break;

			case 'w':
				waitTime = atoi(optarg);
				break;

			default:
				fprintf(stderr, "ERROR: Unknown input %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	printf("Preparing to load data from %d file(s) following format %s, with %d packets per iteration.\n", numPorts, inputFile, config[0].packetsPerIteration);

	printf("These will be loaded into the ringbuffers starting at %d with an offset of %d via ", config[0].key, offset);
	if (packets) {
		printf("UDP packets starting on host/port %s:%d with an offset of 1. \n", config[0].hostname, config[0].portNum);
	} else {
		printf("copying data directly to the ringbuffer.\n\n");
	}


	config[0].portBufferSize = 4 * PACKET_SIZE * config[0].packetsPerIteration;
	config[0].params = &(params[0]);

	for (int port = 1; port < numPorts; port++) {
		strcpy(config[port].hostname, config[0].hostname);
		config[port].portNum = config[0].portNum + port * portOffset;

		config[port].packetsPerIteration = config[0].packetsPerIteration;
		config[port].portBufferSize = config[0].portBufferSize;

		config[port].key = config[0].key + offset * port;
		config[port].params = &(params[port]);

		ringbuffer[port] = IPCIO_INIT;
		header[port] = IPCIO_INIT;
		config[port].ringbuffer = &ringbuffer[port];
		config[port].header = &header[port];
		config[port].nbufs = 16;
		config[port].bufsz = PACKET_SIZE * config[0].packetsPerIteration;
	}


	if (packets == 1) {
		for (int port = 1; port < numPorts; port++) {
			if (ilt_dada_initialise_port(&(config[port])) < 0) {
				return 1;
			}
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

		printf("Allocating %ld MB for %d %d byte packets on port %d\n", (long) config[0].packetsPerIteration * PACKET_SIZE >> 20, config[0].packetsPerIteration, PACKET_SIZE, port);
		
		if (packets == 0) {
			rawData[port] = calloc(config[0].packetsPerIteration * PACKET_SIZE, sizeof(char));

			if (rawData[port] == NULL) {
				fprintf(stderr, "ERROR: Failed to allocate data on port %d, exiting.\n", port);
				return 1;
			}

			if (ilt_dada_initialise_ringbuffer_hdu(&(config[port])) < 0) {
				return 1;
			}
		} else {
			if (ilt_data_operate_prepare(&(config[port])) < 0) {
				return 1;
			}
		}



	}



	while (packetCount < totalPackets && fullReads) {
		for (int port = 0; port < numPorts; port++) {

			datums = fread(rawData[port], sizeof(char), config[port].packetsPerIteration * PACKET_SIZE, inputFiles[port]);

			if (datums != config[port].packetsPerIteration * PACKET_SIZE) {
				fullReads = 0;
				printf("Read less data than expected (%ld, %ld), finishing up.\n", datums, (long) config[port].packetsPerIteration * PACKET_SIZE);
			}

			if (packets == 1) {
				writtenBytes = sendmmsg(config[port].sockfd, config[port].params->msgvec, config[port].packetsPerIteration, 0);
				writtenBytes *= PACKET_SIZE;
			} else {
				writtenBytes = ipcio_write(&(ringbuffer[port]), rawData[port], datums);
			}

			if (writtenBytes != datums) {
				fprintf(stderr, "WARNING Port %d: Tried to send/write %ld bytes to buffer but only wrote %ld.\n", port, datums, writtenBytes);
			} else {
				printf("Port %d: sent/wrote %d packets to buffer.\n", port, config[port].packetsPerIteration);
			}
		}ilt_dada_operate_cleanup(config);
		printf("\n\n");
		usleep(1000 * waitTime);
	}


	for (int port = 0; port < numPorts; port++) {
		printf("Freeing memory/closing file for port %d\n", port);

		ilt_dada_cleanup(&(config[port]));
		free(rawData[port]);	
		fclose(inputFiles[port]);
	}


}
