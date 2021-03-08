#include "ilt_dada_cli.h"


int main(int argc, char  *argv[]) {

	ilt_dada_config cfg = ilt_dada_config_default;
	
	// Initialise some defaults
	cfg.portNum = 16130;
	cfg.key = 16130;
	cfg.packetsPerIteration = 256;
	cfg.portBufferSize = 8 * cfg.packetsPerIteration * MAX_UDP_LEN;
	cfg.bufsz = cfg.packetsPerIteration * MAX_UDP_LEN;
	cfg.nbufs = 128;


	char inputOpt;
	int targetSeconds = 0;

	while ((inputOpt = getopt(argc, argv, "p:k:n:b:s:")) != -1) {
		switch (inputOpt) {

			case 'p':
				cfg.portNum = atoi(optarg);
				break;

			case 'k':
				cfg.key = atoi(optarg);
				break;

			case 'n':
				cfg.packetsPerIteration = atoi(optarg);
				cfg.portBufferSize = 8 * cfg.packetsPerIteration * MAX_UDP_LEN;
				cfg.bufsz = cfg.packetsPerIteration * MAX_UDP_LEN;
				break;

			case 'b':
				if (targetSeconds == 1) {
					fprintf(stderr, "ERROR: Number of buffers is controlled by the number of seconds set when using -s, ignoring -b input.\n");
					break;
				}

				targetSeconds = -1;
				cfg.nbufs = atoi(optarg);
				break;

			case 's':
				if (targetSeconds == -1) {
					fprintf(stderr, "ERROR: Number of buffers is controlled by the number of buffers set when using -b, ignoring -s input.\n");
					break;
				}

				cfg.nbufs = (int) ((float) atoi(optarg) / ((float) cfg.packetsPerIteration / (float) 12207));
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	printf("Preparing ILTDada to record data from port %d, consuming %d packets per iteration.\n", cfg.portNum, cfg.packetsPerIteration);
	printf("Ring buffer will require %ld MB (%ld GB) of memory to hold ~%ld seconds of data.\n", cfg.bufsz * cfg.nbufs >> 20, cfg.bufsz * cfg.nbufs >> 30, cfg.packetsPerIteration * cfg.nbufs / 12207);

	printf("\n\nInitialising UDP port...\n");
	if ((cfg.sockfd = ilt_dada_initialise_port(&cfg)) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Initialisng ringbuffer...\n");
	if (ilt_dada_initialise_ringbuffer(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Checking initial packets...\n");
	if (ilt_dada_check_network(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Starting recording...\n");
	if (ilt_dada_operate(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Cleaning up.\n");
	ilt_dada_cleanup(&cfg);
}
