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
	float targetSeconds = 5.0f, obsSeconds = 60.0f;
	char startTime[DEF_STR_LEN] = "", endTime[DEF_STR_LEN] = "";

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
				obsSeconds = atof(optarg);
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

	// If we haven't been passed a time, set it to the current time.
	if (strcmp(startTime, "") == 0) {
		time_t currTime;
		time(&currTime);
		strftime(startTime, sizeof startTime, "%Y-%m-%dT%H:%M:%S", gmtime(&currTime));

		printf("INFO: Input time not set, setting start time to %s\n.", startTime);
	}

	// Convert the start time to a packet
	cfg.startPacket = getStartingPacket(startTime, clock);

	if (strcmp(endTime, "") != 0) {
		cfg.endPacket = getStartingPacket(endTime, clock);

		if (obsSeconds != 60.0f) {
			fprintf(stderr, "WARNING: Ignoring input observation length (%f) and using input end time instead (%s).\n", obsSeconds, endTime);
		}
	} else {
		cfg.endPacket = cfg.startPacket + (obsSeconds * (clock160MHzPacketRate * (1 - clock) + clock200MHzPacketRate * clock));
	}

	cfg.bufsz = bufferMul * cfg.packetsPerIteration * MAX_UDP_LEN;
	

	if (((float) bufferMul * cfg.packetsPerIteration) / (float) 12207 > targetSeconds) {
		fprintf(stderr, "ERROR: Requested time is less than the size of a single buffer(%f vs %f); increase -s or decrease -m, exiting.\n", ((float) cfg.bufsz / MAX_UDP_LEN) / (float) 12207, targetSeconds);
		return 1;
	}
	cfg.nbufs = targetSeconds * 12207 / cfg.packetsPerIteration / bufferMul;


	printf("Preparing ILTDada to record data from port %d, consuming %d packets per iteration.\n", cfg.portNum, cfg.packetsPerIteration);
	printf("Ring buffer on key  %d (ptr %x) will require %ld MB (%ld GB) of memory to hold ~%ld seconds of data in %" PRIu64 " buffers.\n", cfg.key, cfg.key, cfg.bufsz * cfg.nbufs >> 20, cfg.bufsz * cfg.nbufs >> 30, cfg.packetsPerIteration * cfg.nbufs / 12207, cfg.nbufs);
	printf("Start/End packets will be %ld and %ld.\n\n", cfg.startPacket, cfg.endPacket);

	printf("\n\nInitialising UDP port...\n");
	if (ilt_dada_initialise_port(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Initialisng ringbuffer...\n");
	if (ilt_dada_initialise_ringbuffer(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Preparing to recording...\n");
	if (ilt_dada_operate(&cfg) < 0) {
		printf("Exiting.\n");
		return 1;
	}

	printf("Observation finished, cleaning up.\n");
	ilt_dada_cleanup(&cfg);
}
