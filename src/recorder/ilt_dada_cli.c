#include "ilt_dada_cli.h"

time_t unixTimeFromString(char *inputStr);
int ilt_dada_cli_check_times(char *startTime, char *endTime, double obsSeconds, int ignoreTimeCheck, int minStartup);

void helpMessgaes() {
	printf("ILTDada CLI (CLI v%s, lib %s)\n\n", ILTD_CLI_VERSION, ILTD_VERSION);

	printf("-h      :   Display this message\n\n");

	printf("-p (int):	UDP port to monitor (default: %d)\n", DEF_PORT);
	printf("-k (int):	Output PSRDADA Ringbuffer key (default: %d)\n\n", DEF_PORT);

	printf("-n (int):   Number of packets per network operation (default: 256)\n");
	printf("-m (int):   Number of packets blocks per segment of the ringbuffer (default: 64)\n");
	printf("-s (float): Target ringbuffer length in seconds (determines number of segments in the ringbuffer, default: 5.0)\n\n");

	printf("-r (int):   Number of read clients (default: 1)\n");
	printf("-e      :   Allocate the ringbuffer immediately (default: false)\n");
	printf("-f      :   Force allocate the ringbuffer (remove existing ringbuffer on given key) (default: false)\n\n");

	printf("-S (str):   ISOT Start Time (YYYY-MM-DDTHH:MM:SS, default '')\n");
	printf("-T (str):	ISOT End time (YYYY-MM-DDTHH:MM:SS, default '')\n");
	printf("-t (float):	Observation length in seconds (default: 60s)\n");
	printf("-w (float): Time buffer between accepting packets and starting recording (process will sleep until N seconds before observation, default: 10s)\n");


	/* Undocumented / debug only
	printf("-C     :    Disable end of observation time check\n");

	 */
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
	int bufferMul = 64, packetSizeCopy = -1, minStartup = 60, ignoreTimeCheck = 0;
	float targetSeconds = 5.0f, obsSeconds = 10.0f;
	char startTime[DEF_STR_LEN] = "", endTime[DEF_STR_LEN] = "";

	while ((inputOpt = getopt(argc, argv, "p:k:n:m:s:r:efS:T:t:C")) != -1) {
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

			case 'r':
				cfg->io->dadaConfig.num_readers = atoi(optarg);
				break;

			case 'e':
				cfg->io->progressWithExisting = 1;
				break;

			case 'f':
				cfg->forceStartup = 1;
				break;

			case 'S':
				strcpy(startTime, optarg);
				break;

			case 'T':
				strcpy(endTime, optarg);
				break;

			case 't':
				obsSeconds = atof(optarg);
				break;



			case 'l':
				cfg->packetSize = atoi(optarg);
				packetSizeCopy = atoi(optarg);
				break;

			case 'w':
				minStartup = atoi(optarg);
				break;

			case 'C':
				ignoreTimeCheck = 1;
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				ilt_dada_cleanup(cfg);
				return 1;
		}
	}



	cfg->io->writeBufSize[0] = bufferMul * cfg->packetsPerIteration * cfg->packetSize;

	// TODO: float packetRate = ();	
	if (((float) bufferMul * cfg->packetsPerIteration) / (float) 12207 > targetSeconds) {
		fprintf(stderr, "ERROR: Requested time is less than the size of a single buffer(%f vs %f); increase -s or decrease -m, exiting.\n", ((float) cfg->io->writeBufSize[0] / cfg->packetSize) / (float) 12207, targetSeconds);
		ilt_dada_cleanup(cfg);
		return 1;
	}
	cfg->io->dadaConfig.nbufs = targetSeconds * 12207 / cfg->packetsPerIteration / bufferMul;
	printf("%d, %ld, %d, %f -> %ld\n", bufferMul, cfg->io->dadaConfig.nbufs, cfg->packetsPerIteration, targetSeconds, cfg->io->writeBufSize[0]);


	if (ilt_dada_cli_check_times(startTime, endTime, obsSeconds, ignoreTimeCheck, minStartup) < 0) {
		ilt_dada_cleanup(cfg);
		return 1;
	}

	printf("Setting up networking");
	if (cfg->packetSize != -1) {
		printf(" and ringbuffers");
	}
	printf(".\n");

	if (ilt_dada_setup(cfg, cfg->packetSize != -1) < 0) {
		ilt_dada_cleanup(cfg);
		return 1;
	}

	// TODO: Rework / add clock bit flag so we can test this before we enter a sleep state
	// Convert the start time to a packet
	cfg->startPacket = lofar_udp_time_get_packet_from_isot(startTime, cfg->obsClockBit);

	if (strcmp(endTime, "") != 0) {
		cfg->endPacket = lofar_udp_time_get_packet_from_isot(endTime, cfg->obsClockBit);

	}


	if (cfg->packetSize != packetSizeCopy && packetSizeCopy != -1) {
		fprintf(stderr, "ERROR: Provided packet length differs from obseved packet length (%d vs %d), this may cause issues. Attempting to continue...\n", packetSizeCopy, cfg->packetSize);
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

time_t unixTimeFromString(char *inputStr) {
	struct tm tmTime;
	
	if (strptime(inputStr, "%Y-%m-%dT%H:%M:%S", &tmTime) == NULL) {
		return -1;
	}

	return mktime(&tmTime);
}

int ilt_dada_cli_check_times(char *startTime, char *endTime, double obsSeconds, int ignoreTimeCheck, int minStartup) {
	time_t currTime, startUnixTime, endUnixTime;
	time(&currTime);
	// If we haven't been passed a time, set it to the current time.
	if (strcmp(startTime, "") == 0) {
		strftime(startTime, DEF_STR_LEN * sizeof(char), "%Y-%m-%dT%H:%M:%S", gmtime(&currTime));

		printf("INFO: Input time not set, setting start time to current time of %s\n.", startTime);
	}

	
	if ((startUnixTime = unixTimeFromString(startTime)) == -1) {
		fprintf(stderr, "ERROR: Failed to convert input start time %s to a unix timstamp, exiting.\n", startTime);
		return 1;
	}

	if (strcmp(endTime, "") == 0) {
		endUnixTime = startUnixTime + obsSeconds;
		strftime(endTime, DEF_STR_LEN * sizeof(char), "%Y-%m-%dT%H:%M:%S", gmtime(&endUnixTime));

		printf("INFO: End time set to %s\n.", endTime);
	} else {
		if ((endUnixTime = unixTimeFromString(endTime)) == -1) {
			fprintf(stderr, "ERROR: Failed to convert input end time %s to a unix timstamp, exiting.\n", endTime);
			return -1;
		}

		if (obsSeconds != 60.0f) {
			fprintf(stderr, "WARNING: Ignoring input observation length (%lf) and using input end time instead (%s).\n", obsSeconds, endTime);
		}
	}

	if (!ignoreTimeCheck) {
		if (currTime > endUnixTime) {
			fprintf(stderr, "ERROR: End time %s has already passed, exiting.\n\n", endTime);
			return -1;
		}
	}


	if (endUnixTime < startUnixTime) {
		fprintf(stderr, "ERROR: End time %s is before start time %s, exiting.\n", endTime, startTime);
		return -1;
	}

	if ((startUnixTime - currTime) > minStartup) {
		printf("Observation starts in %ld seconds, sleeping until %d seconds before the start time.\n", (startUnixTime - currTime), minStartup);
		ilt_dada_sleep(startUnixTime - currTime);
	}

	return 0;
}