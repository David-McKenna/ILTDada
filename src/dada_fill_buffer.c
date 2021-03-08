// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

// UPM defines are needed
#include "lofar_udp_general.h"

// PSRDADA includes
#include "ipcio.h"
#include "multilog.h"
#include <stdint.h> // For uint64_t support

#define PACKET_SIZE (UDPHDRLEN + UDPNPOL * UDPNTIMESLICE * 122)

int main(int argc, char *argv[]) {

	int inputOpt, packets = 0, dadaInput;
	char inputFile[2048] = "", workingName[2048] = "";
	long numPackets = 65536, totalPackets = LONG_MAX, packetCount = 0, writtenBytes, datums;
	int numPorts = 1;
	int key = 16130, offset = 10, port = 16130, fullReads = 1;

	char* rawData[MAX_NUM_PORTS];
	FILE *inputFiles[MAX_NUM_PORTS];
	ipcio_t *ringbuffer[MAX_NUM_PORTS];
	ipcio_t *header[MAX_NUM_PORTS];

	while((inputOpt = getopt(argc, argv, "ui:p:n:k:t:")) != -1) {
		switch(inputOpt) {

			case 'u':
				packets = 1;
				port = atoi(optarg);
				break;
			
			case 'i':
				strcpy(inputFile, optarg);
				break;

			case 'p':
				numPackets = atoi(optarg);
				break;

			case 'n':
				numPorts = atoi(optarg);

				if (numPorts > MAX_NUM_PORTS) {
					fprintf(stderr, "ERROR: You requested %d ports, but LOFAR can only produce %d. Exiting.\n", numPorts, MAX_NUM_PORTS);
					return 1;
				}
				break;

			case 'k':
				dadaInput = sscanf(optarg, "%d,%d", &key, &offset);
				break;

			case 't':
				totalPackets = atol(optarg);


			default:
				fprintf(stderr, "ERROR: Unknown input %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	printf("Preparing to load data from %d file(s) following format %s, with %ld packets per iteration.\n", numPorts, inputFile, numPackets);

	printf("These will be loaded into the ringbuffers starting at %d with an offset of %d via ", key, offset);
	if (packets) {
		printf("UDP packets starting on port %d with an offset of 1. \n", port);
	} else {
		printf("copying data directly to the ringbuffer.\n\n");
	}


	for (int i = 0; i < numPorts; i++) {
		sprintf(workingName, inputFile, i);

		printf("Opening file at %s...\n", workingName);
		inputFiles[i] = fopen(workingName, "r");
		if (inputFiles[i] == NULL) {
			fprintf(stderr, "Input file at %s does not exist, exiting.\n", workingName);
			return 1;
		}

		printf("Allocating %ld MB for %ld %d byte packets on port %d\n", numPackets * PACKET_SIZE >> 20, numPackets, PACKET_SIZE, i);
		rawData[i] = calloc(numPackets * PACKET_SIZE, sizeof(char));

		if (rawData[i] == NULL) {
			fprintf(stderr, "ERROR: Failed to allocate data on port %d, exiting.\n", i);
		}

		ringbuffer[i] = malloc(sizeof(ipcio_t));
		*(ringbuffer[i]) = IPCIO_INIT;
		printf("Opening DADA writer on key %x...\n", key + i * offset);
		// Create  and connect to the ringbuffer instance
		if (ipcio_create(ringbuffer[i], key + i * offset, 16, PACKET_SIZE * numPackets, 1) < 0) {
			// ipcio_create(...) prints error to stderr, so we just need to exit.
			return -2;
		}

		header[i] = malloc(sizeof(ipcio_t));
		*(header[i]) = IPCIO_INIT;
		printf("Opening DADA header on key %x...\n", key + i * offset + 1);
		// Create  and connect to the header buffer instance
		if (ipcio_create(header[i], key + i * offset + 1, 1, 4096, 1) < 0) {
			// ipcio_create(...) prints error to stderr, so we just need to exit.
			return -2;
		}

		printf("Enabling DADA writes on key %x...\n", key + i * offset);
		// Open the ringbuffer instance as the primary writer
		if (ipcio_open(ringbuffer[i], 'W') < 0) {
			// ipcio_open(...) prints error to stderr, so we just need to exit.
			return -2;
		}

		printf("Enabling DADA header writes on key %x...\n", key + i * offset);
		// Open the header buffer instance as the primary writer
		if (ipcio_open(header[i], 'W') < 0) {
			// ipcio_open(...) prints error to stderr, so we just need to exit.
			return -2;
		}
	}


	if (packets == 0) {
		while (packetCount < totalPackets && fullReads) {
			for (int i = 0; i < numPorts; i++) {
				datums = fread(rawData[i], sizeof(char), numPackets * PACKET_SIZE, inputFiles[i]);
				if (datums != numPackets * PACKET_SIZE) {
					fullReads = 0;
					printf("Read less data than expected (%ld, %ld), finishing up.\n", datums, numPackets * PACKET_SIZE);
				}

				writtenBytes = ipcio_write(ringbuffer[i], rawData[i], datums);

				if (writtenBytes != datums) {
					fprintf(stderr, "WARNING Port %d: Tried to write %ld bytes to buffer but only wrote %ld.\n", i, datums, writtenBytes);
				} else {
					printf("Port %d: wrote %ld packets to buffer.\n", i, numPackets);
				}
			}
			printf("\n\n");
//			usleep(1000 * 100);
		}
	} else {
		fprintf(stderr, "ERROR: UDP packet implementation not not created, exiting.\n");
	}

	for (int i = 0; i < numPorts; i++) {
		printf("Freeing memory/closing file for port %d\n", i);

		ipcio_close(ringbuffer[i]);
		ipcio_close(header[i]);

		sleep(1);
		ipcio_destroy(ringbuffer[i]);
		ipcio_destroy(header[i]);
		free(rawData[i]);	
		fclose(inputFiles[i]);
	}


}