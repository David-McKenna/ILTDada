#include "ilt_dada_cli.h"

#define DEF_PORT 16130

void helpMessgaes() {
	printf("ILTDada CLI (CLI v%s, lib %s)\n\n", ILTD_CLI_VERSION, ILTD_VERSION);

	printf("-p (int):	UDP port to monitor (default: %d)\n", DEF_PORT);
	printf("-k (int):	Output PSRDADA Ringbuffer key (default: %d)\n\n", DEF_PORT);

	printf("-n");
	printf("-m");
	printf("-s");
	printf("-r");

	printf("-S");
	printf("-T (str):	ISOT End time (YYYY-MM-DDTHH:MM:SS, default '')\n");
	printf("-t (float):	Observation length in seconds (default: 60s)\n");
}

int main(int argc, char  *argv[]) {

	ilt_dada_config *cfg = ilt_dada_init();
	
	// Initialise some defaults
	cfg->portNum = DEF_PORT;
	cfg->io->outputDadaKeys[0] = DEF_PORT;
	cfg->packetsPerIteration = 256;
	cfg->portBufferSize = 8 * cfg->packetsPerIteration * MAX_UDP_LEN;
	cfg->io->writeBufSize[0] = cfg->packetsPerIteration * MAX_UDP_LEN;
	cfg->io->numOutputs = 1;
	cfg->io->dadaConfig.nbufs = 128;


	char inputOpt;
	int bufferMul = 64, packetSizeCopy = -1;
	float targetSeconds = 5.0f, obsSeconds = 60.0f;
	char startTime[DEF_STR_LEN] = "", endTime[DEF_STR_LEN] = "";

	while ((inputOpt = getopt(argc, argv, "p:k:n:m:s:t:S:T:r:fl:")) != -1) {
		switch (inputOpt) {

			case 'p':
				cfg->portNum = atoi(optarg);
				break;

			case 'k':
				cfg->io->outputDadaKeys[0] = atoi(optarg);
				break;

			case 'n':
				cfg->packetsPerIteration = atoi(optarg);
				cfg->portBufferSize = 8 * cfg->packetsPerIteration * MAX_UDP_LEN;
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
				cfg->io->dadaConfig.num_readers = atoi(optarg);
				break;

			case 'f':
				cfg->forceStartup = 1;
				break;

			case 'l':
				cfg->packetSize = atoi(optarg);
				packetSizeCopy = atoi(optarg);
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				ilt_dada_cleanup(cfg);
				return 1;
		}
	}

	cfg->io->writeBufSize[0] = bufferMul * cfg->packetsPerIteration * cfg->packetSize;
	

	if (((float) bufferMul * cfg->packetsPerIteration) / (float) 12207 > targetSeconds) {
		fprintf(stderr, "ERROR: Requested time is less than the size of a single buffer(%f vs %f); increase -s or decrease -m, exiting.\n", ((float) cfg->io->writeBufSize[0] / cfg->packetSize) / (float) 12207, targetSeconds);
		return 1;
	}
	cfg->io->dadaConfig.nbufs = targetSeconds * 12207 / cfg->packetsPerIteration / bufferMul;

	printf("Setting up networking...\n");
	if (ilt_dada_setup(cfg, cfg->bufAssumePacketLength > 0) < 0) {
		ilt_dada_cleanup(cfg);
		return 1;
	}

	// If we haven't been passed a time, set it to the current time.
	if (strcmp(startTime, "") == 0) {
		time_t currTime;
		time(&currTime);
		strftime(startTime, sizeof startTime, "%Y-%m-%dT%H:%M:%S", gmtime(&currTime));

		printf("INFO: Input time not set, setting start time to %s\n.", startTime);
	}

	// Convert the start time to a packet
	cfg->startPacket = lofar_udp_time_get_packet_from_isot(startTime, cfg->obsClockBit);

	if (strcmp(endTime, "") != 0) {
		cfg->endPacket = lofar_udp_time_get_packet_from_isot(endTime, cfg->obsClockBit);

		if (obsSeconds != 60.0f) {
			fprintf(stderr, "WARNING: Ignoring input observation length (%f) and using input end time instead (%s).\n", obsSeconds, endTime);
		}
	} else {
		cfg->endPacket = cfg->startPacket + (obsSeconds * (clock160MHzPacketRate * (1 - cfg->obsClockBit) + clock200MHzPacketRate * cfg->obsClockBit));
	}



	if (cfg->packetSize != packetSizeCopy && packetSizeCopy != -1) {
		fprintf(stderr, "ERROR: Provided packet length differs from obseved packet length (%d vs %d), this may cause issues. Attempting to continue...\n", cfg->bufAssumePacketLength, cfg->packetSize);
	}

	printf("Preparing ILTDada to record data from port %d, consuming %d packets per iteration.\n", cfg->portNum, cfg->packetsPerIteration);
	printf("Ring buffer on key  %d (ptr %x) will require %ld MB (%ld GB) of memory to hold ~%ld seconds of data in %" PRIu64 " buffers.\n", cfg->io->outputDadaKeys[0], cfg->io->outputDadaKeys[0], cfg->io->writeBufSize[0] * cfg->io->dadaConfig.nbufs >> 20, cfg->io->writeBufSize[0] * cfg->io->dadaConfig.nbufs >> 30, cfg->packetsPerIteration * cfg->io->dadaConfig.nbufs / 12207, cfg->io->dadaConfig.nbufs);
	printf("Start/End packets will be %ld and %ld.\n\n", cfg->startPacket, cfg->endPacket);

	printf("Preparing to start recording...\n");
	if (ilt_dada_operate(cfg) < 0) {
		printf("Exiting.\n");
		ilt_dada_cleanup(cfg);
		return 1;
	}

	printf("Observation finished, cleaning up.\n");
	ilt_dada_cleanup(cfg);
}
