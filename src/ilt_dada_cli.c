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
	int bufferMul = 64;
	float targetSeconds = 0.0;

	while ((inputOpt = getopt(argc, argv, "p:k:n:m:s:t:r:")) != -1) {
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
				break;

			case 'm':
				bufferMul = atoi(optarg);
				break;

			case 's':
				targetSeconds = atof(optarg);
				break;

			case 't':
				cfg.endPacket = atoi(optarg) * 12207;
				break;

			case 'r':
				cfg.num_readers = atoi(optarg);
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	cfg.bufsz = bufferMul * cfg.packetsPerIteration * MAX_UDP_LEN;
	

	if (((float) bufferMul * cfg.packetsPerIteration) / (float) 12207 > targetSeconds) {
		fprintf(stderr, "ERROR: Requested time is less than the size of a single buffer(%f vs %f); increase -s or decrease -m, exiting.\n", ((float) cfg.bufsz / MAX_UDP_LEN) / (float) 12207, targetSeconds);
		return 1;
	}
	cfg.nbufs = targetSeconds * 12207 / cfg.packetsPerIteration / bufferMul;


	printf("Preparing ILTDada to record data from port %d, consuming %d packets per iteration.\n", cfg.portNum, cfg.packetsPerIteration);
	printf("Ring buffer on key  %d (ptr %x) will require %ld MB (%ld GB) of memory to hold ~%ld seconds of data in %" PRIu64 " buffers.\n", cfg.key, cfg.key, cfg.bufsz * cfg.nbufs >> 20, cfg.bufsz * cfg.nbufs >> 30, cfg.packetsPerIteration * cfg.nbufs / 12207, cfg.nbufs);

	printf("\n\nInitialising UDP port...\n");
	if (ilt_dada_initialise_port(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Checking initial packets...\n");
	if (ilt_dada_check_network(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Initialisng ringbuffer...\n");
	if (ilt_dada_initialise_ringbuffer(&cfg) < 0) {
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
