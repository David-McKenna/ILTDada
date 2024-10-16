#include "ilt_dada_cli.h"
#include "lofar_cli_meta.h"

const float DEF_OBS_LENGTH = 60.0f;
const float DEF_BUFFER_TIME = 5.0f;

const int DEF_PACKETS_PER_READ_OP = 256;
const int DEF_ITERS_PER_CONSOLE_WRITE_OP = 256;
const int DEF_NUM_OUTPUT = 1;
const int DEF_NUM_BUFFERS = 64;

void iltd_helpMessages() {
	printf("ILTDada CLI (CLI v%s, lib %s)\n\n", ILTD_CLI_VERSION, ILTD_VERSION);

	printf("-h      :   Display this message\n\n");

	printf("-p (int):   UDP port to monitor (default: %d)\n", DEF_PORT);
	printf("-k (int):   Output PSRDADA Ringbuffer key (default: %d)\n\n", DEF_PORT);

	printf("-n (int):   Number of packets per network operation (default: %d)\n", DEF_PACKETS_PER_READ_OP);
	printf("-m (int):   Number of packets blocks per segment of the ringbuffer (default: %d)\n", DEF_NUM_BUFFERS);
	printf("-s (float): Target ringbuffer length in seconds (determines number of segments in the ringbuffer, default: %f)\n", DEF_BUFFER_TIME);
	printf("-l (int):   Number of packet writes per logging status to console (default: %d)\n", DEF_ITERS_PER_CONSOLE_WRITE_OP);
	printf("-z (float): Network timeout length in seconds (must be greater than 2, default: 30)\n");

	printf("-r (int):   Number of read clients (default: 1)\n");
	printf("-e (int):   Allocate the ringbuffer immediately for a given packet size (default: false, recommended: 7824)\n");
	printf("-f      :   Force allocate the ringbuffer (remove existing ringbuffer on given key) (default: false)\n\n");

	printf("-S (str):   ISOT Start Time (YYYY-MM-DDTHH:MM:SS, default '')\n");
	printf("-T (str):   ISOT End time (YYYY-MM-DDTHH:MM:SS, default '')\n");
	printf("-t (float): Observation length in seconds (default: 60s)\n");
	printf("-w (float): Time buffer between accepting packets and starting recording (process will not perform any actions until N seconds before observation, default: 10s)\n");


	/* Undocumented / debug only
	printf("-C     :    Disable end of observation time check\n");

	 */
}

int main(int argc, char  *argv[]) {

	if (argc == 1) {
		iltd_helpMessages();
		return 1;
	}

	ilt_dada_config *cfg = ilt_dada_init();
	
	// Initialise some defaults
	cfg->portNum = DEF_PORT;
	cfg->io->outputDadaKeys[0] = DEF_PORT;
	cfg->packetsPerIteration = DEF_PACKETS_PER_READ_OP;
	cfg->portBufferSize = 8 * cfg->packetsPerIteration * MAX_UDP_LEN;
	cfg->io->writeBufSize[0] = cfg->packetsPerIteration * MAX_UDP_LEN;
	cfg->io->numOutputs = DEF_NUM_OUTPUT;
	cfg->writesPerStatusLog = DEF_ITERS_PER_CONSOLE_WRITE_OP;


	char inputOpt;
	int bufferMul = DEF_NUM_BUFFERS, packetSizeCopy = -1, minStartup = 60, ignoreTimeCheck = 0;
	float targetSeconds = DEF_BUFFER_TIME, obsSeconds = DEF_OBS_LENGTH;
	char startTime[DEF_STR_LEN] = "", endTime[DEF_STR_LEN] = "";

	char *endPtr = NULL, flagged = 0;

	while ((inputOpt = getopt(argc, argv, "hp:k:n:m:s:r:l:z:e:fS:T:t:C")) != -1) {
		switch (inputOpt) {

			case 'h':
				iltd_helpMessages();
				flagged = 1;
				break;

			case 'p':
				cfg->portNum = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'k':
				cfg->io->outputDadaKeys[0] = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'n':
				cfg->packetsPerIteration = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				cfg->portBufferSize = 8 * cfg->packetsPerIteration * MAX_UDP_LEN;
				break;

			case 'm':
				bufferMul = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 's':
				targetSeconds = strtof(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'r':
				cfg->io->dadaConfig.num_readers = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'l':
				cfg->writesPerStatusLog = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'z':
				cfg->portTimeout = strtof(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;

			case 'e':
				cfg->packetSize = internal_strtoi(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				cfg->forceStartup = 1;
				packetSizeCopy = cfg->packetSize;
				break;

			case 'f':
				cfg->io->progressWithExisting = 1;
				break;

			case 'S':
				strcpy(startTime, optarg);
				break;

			case 'T':
				strcpy(endTime, optarg);
				break;

			case 't':
				obsSeconds = strtof(optarg, &endPtr);
				if (checkOpt(inputOpt, optarg, endPtr)) { flagged = 1; }
				break;


			case 'w':
				minStartup = internal_strtoi(optarg, &endPtr);
				if (checkOpt('w', optarg, endPtr)) { flagged = 1; }
				if (minStartup < 2) {
					fprintf(stderr, "ERROR: Minimum start-up time must be greater than 2 seconds (%s/%d provided), exiting.\n", optarg, minStartup);
					return 1;
				}
				break;

			case 'C':
				ignoreTimeCheck = 1;
				break;

			default:
				fprintf(stderr, "ERROR: Unknown flag %c, exiting.\n", inputOpt);
				ilt_dada_config_cleanup(cfg);
				return 1;
		}
	}

	if (flagged) {
		ilt_dada_config_cleanup(cfg);
		return 1;
	}




	cfg->io->writeBufSize[0] = bufferMul * cfg->packetsPerIteration * cfg->packetSize;

	// TODO: float packetRate = ();
	float packetRate = 12207.0f;
	if (((float) bufferMul * cfg->packetsPerIteration) / packetRate > targetSeconds) {
		fprintf(stderr, "ERROR: Requested time is less than the size of a single buffer (%f vs %f); increase -s or decrease -m, exiting.\n", ((float) bufferMul * cfg->packetsPerIteration) / packetRate, targetSeconds);
		ilt_dada_config_cleanup(cfg);
		return 1;
	}
	cfg->io->dadaConfig.nbufs = targetSeconds * packetRate / (cfg->packetsPerIteration * bufferMul);


	if (ilt_dada_cli_check_times(startTime, endTime, obsSeconds, ignoreTimeCheck, minStartup) < 0) {
		ilt_dada_config_cleanup(cfg);
		return 1;
	}

	printf("Setting up networking");
	if (cfg->packetSize != -1) {
		printf(" and ringbuffers");
	}
	printf(".\n");

	if (ilt_dada_config_setup(cfg, cfg->packetSize != -1) < 0) {
		ilt_dada_config_cleanup(cfg);
		return 1;
	}

	// TODO: Rework / add clock bit flag so we can test this before we enter a sleep state
	// Convert the start time to a packet
	// Fallback to 200MHz clock (bit = 1) if bit is not set.
	cfg->startPacket = lofar_udp_time_get_packet_from_isot(startTime, cfg->obsClockBit > 2 ? 1 : cfg->obsClockBit);

	if (strcmp(endTime, "") != 0) {
		cfg->endPacket = lofar_udp_time_get_packet_from_isot(endTime, cfg->obsClockBit > 2 ? 1 : cfg->obsClockBit);

	}




	if (cfg->packetSize != packetSizeCopy && packetSizeCopy != -1) {
		fprintf(stderr, "ERROR: Provided packet length differs from observed packet length (%d vs %d), this may cause issues. Attempting to continue...\n", packetSizeCopy, cfg->packetSize);
	}

	printf("Preparing ILTDada to record data from port %d, consuming %d packets per iteration.\n", cfg->portNum, cfg->packetsPerIteration);
	printf("Ringbuffer on key %d (ptr %x) will require %ld MB (%ld GB) of memory to hold ~%.1f seconds of data in %" PRIu64 " buffers.\n", cfg->io->outputDadaKeys[0], cfg->io->outputDadaKeys[0], cfg->io->writeBufSize[0] * cfg->io->dadaConfig.nbufs >> 20, cfg->io->writeBufSize[0] * cfg->io->dadaConfig.nbufs >> 30, (bufferMul * cfg->packetsPerIteration * cfg->io->dadaConfig.nbufs) / packetRate, cfg->io->dadaConfig.nbufs);
	printf("Start/End packets will be %ld and %ld.\n\n", cfg->startPacket, cfg->endPacket);

	printf("Preparing to start recording...\n");
	if (ilt_dada_operate(cfg) < 0) {
		printf("Exiting.\n");
		ilt_dada_config_cleanup(cfg);
		return 1;
	}

	printf("Observation finished, cleaning up.\n");
	ilt_dada_config_cleanup(cfg);

	return 0;
}

/**
 * @brief      { function_description }
 *
 * @param      inputStr  The input string
 *
 * @return     { description_of_the_return_value }
 */
time_t unixTimeFromString(const char *inputStr) {
	struct tm tmTime;
	
	if (strptime(inputStr, "%Y-%m-%dT%H:%M:%S", &tmTime) == NULL) {
		return -1;
	}

	return timegm(&tmTime);
}

/**
 * @brief      { function_description }
 *
 * @param      startTime        The start time
 * @param      endTime          The end time
 * @param[in]  obsSeconds       The obs seconds
 * @param[in]  ignoreTimeCheck  The ignore time check
 * @param[in]  minStartup       The minimum startup
 *
 * @return     { description_of_the_return_value }
 */
int ilt_dada_cli_check_times(char *startTime, char *endTime, double obsSeconds, int ignoreTimeCheck, int minStartup) {
	time_t currTime, startUnixTime, endUnixTime;
	
	// Get the current Unix time for reference
	if (time(&currTime) == -1) {
		fprintf(stderr, "ERROR: Failed to get current time, exiting.\n");
		return -1;
	}

	// If we haven't been passed a time, set it to the current time.
	if (strcmp(startTime, "") == 0) {
		strftime(startTime, DEF_STR_LEN * sizeof(char), "%Y-%m-%dT%H:%M:%S", gmtime(&currTime));

		printf("INFO: Input time not set, setting start time to current time of %s.\n", startTime);
	}


	if ((startUnixTime = unixTimeFromString(startTime)) == -1) {
		fprintf(stderr, "ERROR: Failed to convert input start time %s to a unix timstamp, expected format 'YYYY-mm-DDTHH:MM:SS', exiting.\n", startTime);
		return -1;
	}

	if (strcmp(endTime, "") == 0) {
		endUnixTime = startUnixTime + obsSeconds;
		strftime(endTime, DEF_STR_LEN * sizeof(char), "%Y-%m-%dT%H:%M:%S", gmtime(&endUnixTime));

		printf("INFO: End time set to %s from %ld + %lf\n.", endTime, startUnixTime, obsSeconds);
	} else {
		if ((endUnixTime = unixTimeFromString(endTime)) == -1) {
			fprintf(stderr, "ERROR: Failed to convert input end time %s to a unix timstamp, exiting.\n", endTime);
			return -1;
		}

		if (obsSeconds != DEF_OBS_LENGTH) {
			fprintf(stderr, "WARNING: Ignoring input observation length (%lf) and using input end time instead (%s).\n", obsSeconds, endTime);
		}
	}

	if (!ignoreTimeCheck) {
		if (currTime > endUnixTime) {
			fprintf(stderr, "ERROR: End time %s has already passed, exiting.\n\n", endTime);
			return -1;
		}
		
		if (endUnixTime < startUnixTime) {
			fprintf(stderr, "ERROR: End time %s is before start time %s, exiting.\n", endTime, startTime);
			return -1;
		}
	}

	if ((startUnixTime - currTime) > minStartup) {
		printf("Observation starts in %ld seconds, sleeping until %d seconds before the start time.\n", (startUnixTime - currTime), minStartup);
		ilt_dada_sleep(startUnixTime - currTime, 1);
	}

	return 0;
}
