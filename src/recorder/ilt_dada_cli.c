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
	int bufferMul = 64, clock = 1;
	float targetSeconds = 5.0;
	char startTime[1024] = "", endTime[1024] = "";

	while ((inputOpt = getopt(argc, argv, "cp:k:n:m:s:t:S:T:r:")) != -1) {
		switch (inputOpt) {

			case 'c':
				clock = 0;
				break;

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
				cfg.endPacket = (long) atof(optarg) * 12207;
				break;

			case 'S':
				strcpy(startTime, optarg);
				break;

			case 'T':
				strcpy(endTime, optarg);
				break;

			case 'r':
				cfg.num_readers = atoi(optarg);
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				return 1;
		}
	}

	if (strcmp(startTime, "") == 0) {
		time_t currTime;
		time(&currTime);
		// Subtract a second so that we will always start recording immediately
		currTime -= 1;
		strftime(startTime, sizeof startTime, "%Y-%m-%dT%H:%M:%S", gmtime(&currTime));
	}

	cfg.startPacket = getStartingPacket(startTime, clock);

	if (cfg.endPacket > 0) {
		if (strcmp(endTime, "") != 0) {
			fprintf(stderr, "WARNING: Prioritising observation length (%f) over end time stamp (%s).\n", (float) cfg.endPacket / 12207, endTime);
		}
		
		cfg.endPacket += cfg.startPacket;
	} else if (strcmp(endTime, "") != 0) {
		cfg.endPacket = getStartingPacket(endTime, clock);
	}

	printf("%ld, %ld\n", cfg.startPacket, cfg.endPacket);

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
