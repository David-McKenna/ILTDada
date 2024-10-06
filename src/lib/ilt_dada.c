#pragma clang diagnostic push
#pragma ide diagnostic ignored "openmp-use-default-none"

#include "ilt_dada.h"
#include <limits.h>

// Operations struct defaults
const ilt_dada_operate_params ilt_dada_operate_params_default = {
	.packetBuffer = NULL,
	.msgvec = NULL,
	.iovecs = NULL,
	.timeout = NULL,
	
	.packetsSeen = 0,
	.packetsExpected = 0,
	.packetsLastSeen = 0,
	.packetsLastExpected = 0,
	.finalPacket = -1,
	.bytesWritten = 0
};

// Configuration struct defaults
const ilt_dada_config ilt_dada_config_default = {
	// UDP configuration
	.portNum = -1,
	.portBufferSize = -1,
	.portPriority = 6,
	.packetSize = MAX_UDP_LEN,
	.portTimeout = 30,
	.recvflags = 0,


	// Recorder checks configuration
	.forceStartup = 0,
	.checkInitParameters = 1,
	.checkInitData = 1,
	.checkParameters = CHECK_FIRST_LAST,
	.writesPerStatusLog = 256,

	// Observation configuration
	.startPacket = -1,
	.endPacket = -1,
	.currentPacket = -1,
	.packetsPerIteration = 256, // ~0.021 seconds of data
	.obsClockBit= -1,



	// PSRDADA working variables
	.sockfd = -1,
	.headerText = "",

	// Internal state and UPM structs for writing
	.params = NULL,
	.io = NULL,
	.state = 0,
};


// Sleep function wrapper

/**
 * @brief      Sleep for a given amount of time (in seconds)
 *
 * @param[in]  seconds  The seconds to sleep
 * @param[in]  verbose  Enable / disable print message
 */
void ilt_dada_sleep(double seconds, int verbose) {
	if (verbose) {
		printf("Sleeping for %lf seconds.\n", seconds);
	}

	ilt_dada_sleep_multilog(seconds, NULL);
}
/**
 * @brief      Sleep for a given amount of time (in seconds)
 *
 * @param[in]  seconds  The seconds to sleep
 * @param      mlog     The multilog struct if you want to log to the ringbuffer
 */
void ilt_dada_sleep_multilog(double seconds, multilog_t* mlog) {
	// Ensure we have a valid input
	if (seconds <= 0) {
		fprintf(stderr, "ERROR: Negative/Zero time (%lf) passed to %s, continuing.\n", seconds, __func__);
		return;
	}

	// If given, log the sleep to the ringbuffer multilog
	if (mlog != NULL) {
		#pragma omp task
		multilog(mlog, 6, "Sleeping for %lf seconds.\n", seconds);
	}

	// Actually sleep for the given amount of time
	struct timespec sleep = { (time_t) seconds, (long) (seconds - (long) seconds) * 1e9 };
	nanosleep(&sleep, NULL);
}


// Allocate and initialise the configuration, operations and I/O structs
/**
 * @brief      Initialise a ilt_dada_config struct
 *
 * @return     ptr (success), NULL (failure)
 */
ilt_dada_config* ilt_dada_init() {

	// Allocate and null-check the main struct
	ilt_dada_config *config = calloc(1, sizeof(ilt_dada_config));

	if (config == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate memory for configuration struct, exiting.\n");
		return NULL;
	} 

	// Assign the default values
	if (memcpy(config, &(ilt_dada_config_default), sizeof(ilt_dada_config)) != config) {
		fprintf(stderr, "ERROR: Failed to copy config default, exiting.\n");
		ilt_dada_config_cleanup(config);
		return NULL;
	}

	// Allocate and null-check the operations and I/O structs
	config->params = calloc(1, sizeof(ilt_dada_operate_params));
	config->io = lofar_udp_io_write_alloc();

	if (config->params == NULL || config->io == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate memory for configuration struct components, exiting.\n");
		ilt_dada_config_cleanup(config);
		return NULL;
	}

	// Assign the default values, adapted for the recorder
	if (memcpy(config->params, &(ilt_dada_operate_params_default), sizeof(ilt_dada_operate_params)) != config->params) {
		fprintf(stderr, "ERROR: Failed to copy params default, exiting.\n");
		ilt_dada_config_cleanup(config);
		return NULL;
	}
	config->io->readerType = DADA_ACTIVE;

	return config;
}

/**
 * @brief      Initialise a UDP network socket following the given configuration struct
 *
 * @param      config  The ilt_dada_config configuration struct
 *
 * @return     0 (success) / -1 (failure) / -2 (unresolvable failure)
 */
int ilt_dada_initialise_port(ilt_dada_config *config) {
	if (!(config->state & NETWORK_READY)) {
		// http://beej.us/guide/bgnet/pdf/bgnet_usl_c_1.pdf
		// >>> The place most people get stuck around here is what order to call
		// >>> these things in. In that, the man pages are no use, as you’ve
		// >>> probably discovered.
		// Can confirm. This document is a life saver.

		// Ensure we aren't trying to bind to a reserved port
		// Kernel recommends using ports between 1024 and 49151
		if (config->portNum < MIN_PORT || config->portNum > MAX_PORT) {
			fprintf(stderr, "ERROR: Requested a reserved port (%d). Exiting.\n", config->portNum);
			return -2;
		}

		// Bind to an IPv4 or IPv6 connection, using UDP packet paradigm, on a
		// wildcard address
		struct addrinfo addressInfo = {
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
			.ai_flags = IPPROTO_UDP | AI_PASSIVE
		};

		// Convert the port to a string for getaddrinfo
		char portNumStr[16];
		if (snprintf(portNumStr, 15, "%d", config->portNum) < 0) {
			fprintf(stderr, "ERROR: Failed to print port number (%d) to string in %s, exiting.\n", config->portNum, __func__);
			return -2;
		}

		// Struct to collect the results from getaddrinfo
		struct addrinfo *serverInfo;
		int status;

		// Populate the remaining parts of addressInfo
		if ((status = getaddrinfo(NULL, portNumStr, &addressInfo, &serverInfo)) < 0) {
			fprintf(stderr, "ERROR: Failed to get address info on port %d (errno %d: %s).", config->portNum, status, gai_strerror(status));
			return -1;
		}


		// Build our socket from the results of getaddrinfo
		int sockfd_init = -1;
		if ((sockfd_init = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_flags)) == -1) {
			fprintf(stderr, "ERROR: Failed to build socket on port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}

		// Attempt to bind to the socket
		if (config->recvflags != -1) {
			if (bind(sockfd_init, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
				fprintf(stderr, "ERROR: Failed to bind to port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
				cleanup_initialise_port(serverInfo, sockfd_init);
				return -1;
			}
		}

		// We have successfully built and bound to a socket, let's tweak some of
		// it's parameters


		// Check if the port buffer is larger than the requested buffer size.
		// We will then increase the buffer size if it is smaller than bufferSize
		//
		// getsockopt will return 2x the actual buffer size, as it includes extra
		// space to account for the kernel overheads, hence the need to double
		// bufferSize in this comparison
		//
		// https://linux.die.net/man/7/socket
		// https://linux.die.net/man/2/setsockopt
		long optVal = 0;
		unsigned int optLen = sizeof(optVal);
		if (getsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &optVal, &optLen) == -1) {
			fprintf(stderr, "ERROR: Failed to get buffer size on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}

		if (optVal < (2 * config->portBufferSize - 1)) {
			if (setsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &(config->portBufferSize), sizeof(config->portBufferSize)) == -1) {
				fprintf(stderr, "ERROR: Failed to adjust buffer size on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
				cleanup_initialise_port(serverInfo, sockfd_init);
				return -1;
			} else if (getsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &optVal, &optLen) == -1) {
				fprintf(stderr, "ERROR: Unable to validate socket buffer size on port %d (errno %d: %s).\n", config->portNum, errno, strerror(errno));
				cleanup_initialise_port(serverInfo, sockfd_init);
				return -1;
			} else if (optVal < (2 * config->portBufferSize - 1)) {
				fprintf(stderr, "ERROR: Failed to fully adjust buffer size on port %d (attempted to set to %ld, call returned %ld).\n", config->portNum,
				        config->portBufferSize * 2, optVal);
				FILE *rmemMax = fopen("/proc/sys/net/core/rmem_max", "r");
				if (rmemMax != NULL) {
					long rmemMaxVal;
					int dummy = fscanf(rmemMax, "%ld", &rmemMaxVal);
					if (rmemMaxVal < config->portBufferSize) {
						fprintf(stderr,
						        "ERROR: This was because your kernel has the maximum UDP buffer size set to a lower value than you requested (%ld).\nERROR: Please increase the value stored in /proc/sys/net/core/rmem_max if you want to use a larger buffer.\n",
						        rmemMaxVal);
					} else if (dummy <= 0) {
						fprintf(stderr,
						        "ERROR: This may be due to your maximum socket buffer being too low, but we could not read /proc/sys/net/core/rmem_max to verify this.\n");
					}
					fprintf(stderr,
					        "ERROR: You require root access to this machine resolve this issue. Please run the commands `echo 'net.core.rmem_max=%ld' | [sudo] tee -a /etc/sysctl.conf; sudo sysctl -p` to change your kernel properties.\n",
					        (2 * config->portBufferSize - 1));
					fprintf(stderr, "See the README.md for more details on this change.\n");
					fclose(rmemMax);
				}
				cleanup_initialise_port(serverInfo, sockfd_init);
				return -1;
			}
		}


		// Without root permissions we can increase the port priority up to 6
		// If we are below the value set by portPriority, adjust the port priority
		// to the given value
		if (getsockopt(sockfd_init, SOL_SOCKET, SO_PRIORITY, &optVal, &optLen) == -1) {
			fprintf(stderr, "ERROR: Failed to get port priority on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}

		if (optVal < config->portPriority) {
			if (setsockopt(sockfd_init, SOL_SOCKET, SO_PRIORITY, &(config->portPriority), sizeof(config->portPriority)) == -1) {
				fprintf(stderr, "ERROR: Failed to adjust port priority on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
				cleanup_initialise_port(serverInfo, sockfd_init);
				return -1;
			}
		}


		// Allow the port to be re-used, encase we are slow to cleanup after the end
		// of our observation (either on our end or the process consuming the ringbuffer).
		const int allowReuse = 1;
		if (setsockopt(sockfd_init, SOL_SOCKET, SO_REUSEADDR, &allowReuse, sizeof(allowReuse)) == -1) {
			fprintf(stderr, "ERROR: Failed to set port re-use property on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}

		// Set a hard cap on the timeout for receiving data from the socket. We can't fully
		// 	trust recvmmsg here due to a known bug where the N_packs - 1 packet may block
		// 	infinitely if it is never received
		// 	https://man7.org/linux/man-pages/man2/recvmmsg.2.html#bugs
		const struct timeval timeout = { .tv_sec = (time_t) config->portTimeout, .tv_usec = (suseconds_t) ((config->portTimeout - ((long) config->portTimeout)) *
		                                                                                              1e6) };
		if (setsockopt(sockfd_init, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
			fprintf(stderr, "ERROR: Failed to set timeout on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}

		// Cleanup the addrinfo linked list before returning
		cleanup_initialise_port(serverInfo, -1);
		// Return the socket fd and exit
		config->sockfd = sockfd_init;
		config->state |= NETWORK_READY;
	}

	return 0;
}

/**
 * @brief      Cleanup on failure/completion of ilt_dada_initialise_port
 *
 * @param      serverInfo   The populated addrinfo struct
 * @param[in]  sockfd_init  The socket file descriptor or -1
 */
void cleanup_initialise_port(struct addrinfo *serverInfo, int sockfd_init) {
	// getaddrinfo created a linked list; clean it up now that we no longer need
	// it.
	if (serverInfo != NULL) {
		freeaddrinfo(serverInfo);
	}

	// Close the socket if it was successfully created
	if (sockfd_init != -1) {
		shutdown(sockfd_init, SHUT_RDWR);
	}
}




// Ringbuffer References:
// http://psrdada.sourceforge.net/manuals/Specification.pdf (outdated and many examples no longer work)
// More luck looking at the headers/code:
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/dada_hdu.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/ipcio.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/multilog.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/ascii_header.h


/**
 * @brief      Sanity check parameters before they are used
 *
 * @param      config  The ilt_dada_config struct
 * @param	   expectedState Combination of config_states the struct should be in
 *
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_check_config(ilt_dada_config *config, config_states expectedState) {
	// int portNum;
	if (config->portNum < MIN_PORT) {
		fprintf(stderr, "ERROR: Port number is too low (given %d, limit %d).\n", config->portNum, MIN_PORT);
		return -1;
	} else if (config->portNum > MAX_PORT) {
		fprintf(stderr, "ERROR: Port number is too high (given %d, limit %d).\n", config->portNum, MAX_PORT);
		return -1;
	}

	// long portBufferSize;
	if (config->portBufferSize < 1) {
		fprintf(stderr, "ERROR: Memory buffer does not appear to be set (%ld).\n", config->portBufferSize);
		return -1;
	}

	// int portPriority;
	if (config->portPriority < 0) {
		fprintf(stderr, "ERROR: Port priority is negative (%d).\n", config->portPriority);
		return -1;
	}

	// int packetSize;
	if (config->packetSize < UDPHDRLEN) {
		fprintf(stderr, "ERROR: Packet size is too small to be useful (%d).\n", config->packetSize);
		return -1;
	} else if (config->packetSize > MAX_UDP_LEN) {
		fprintf(stderr, "ERROR: Packet size is larger than maximum (%d vs %d).\n", config->packetSize, MAX_UDP_LEN);
		return -1;
	}

	// float portTimeout;
	if (config->portTimeout < 2) {
		fprintf(stderr, "ERROR: Port timeout is too small (%f, must be greater than 2).\n", config->portTimeout);
		return -1;
	}

	// int recvflags;
	if (config->recvflags & MSG_PEEK) {
		fprintf(stderr, "ERROR: Peeking is enabled for main packet reads (this will result in packets being read multiple times).\n");
		return -1;
	}

	// ILTDada runtime options
	// int forceStartup;
	if (config->forceStartup < 0 || config->forceStartup > 1) {
		fprintf(stderr, "ERROR: forceStartup is not in a boolean state (%d).", config->forceStartup);
		return -1;
	}

	// int checkInitParameters;
	// int checkInitData;
	// check_parameter_types checkParameters;

	// int writesPerStatusLog;
	if (config->writesPerStatusLog < 0) {
		fprintf(stderr, "ERROR: writesPerStatusLog is negative (%d).\n", config->writesPerStatusLog);
		return -1;
	}

	// Observation configuration

	// long startPacket;
	// long endPacket;
	if (config->state & NETWORK_READY) {
		if (config->startPacket < LFREPOCH) {
			fprintf(stderr, "ERROR: Start packet is before 2008 (uninitialised? %ld).", config->startPacket);
			return -1;
		}

		if (config->endPacket < LFREPOCH) {
			fprintf(stderr, "ERROR: End packet is before 2008 (uninitialised? %ld).", config->endPacket);
			return -1;
		}
	}

	// int packetsPerIteration;
	if (config->packetsPerIteration < 1) {
		fprintf(stderr, "ERROR: Requested less than 1 packet per read operation (%d).\n", config->packetsPerIteration);
		return -1;
	}



	// unsigned char obsClockBit;


	// Ringbuffer working variables
	// int sockfd;
	if (config->state & NETWORK_READY) {
		if (config->obsClockBit > 2) {
			fprintf(stderr, "ERROR: obsClockBit does not appear to be correctly set (%d)\n", config->obsClockBit);
		}

		if (config->sockfd < 0) {
			fprintf(stderr, "ERROR: state indicates network is ready, but socket is not set.\n");
			return -1;
		}
	}


	// char headerText[DADA_DEFAULT_HEADER_SIZE];

	// Main operation loop variables
	// ilt_dada_operate_params *params;
	if (config->params == NULL) {
		fprintf(stderr, "ERROR: Parameters struct does not appear to be allocated.\n");
		return -1;
	}

	// lofar_udp_io_write_config *io;
	if (config->io == NULL) {
		fprintf(stderr, "ERROR: I/O struct does not appear to be allocated.\n");
		return -1;
	}

	// config_states state;
	if (config->state != expectedState) {
		fprintf(stderr, "ERROR: State does not match expectations, (NTWK_R: %d vs %d, NTWK_C: %d v %d, RBR_R: %d vs %d, CPL: %d vs %d).\n",
		  expectedState & NETWORK_READY, config->state & NETWORK_READY, expectedState & NETWORK_CHECKED, config->state & NETWORK_CHECKED,
		  expectedState & RINGBUFFER_READY, config->state & RINGBUFFER_READY, expectedState & COMPLETE, config->state & COMPLETE);
		return -1;
	}
	return 0;
}

/**
 * @brief      Setup a ringbuffer from only ilt_dada_config
 *
 * @param      config    The ilt_dada configuration struct
 * @param[in]  setup_io  Choose to allocate the ringbuffer or not
 *
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_config_setup(ilt_dada_config *config, int setup_io) {

	// Sanity check the input
	if (ilt_dada_check_config(config, 0) < 0) {
		return -1;
	}

	// Initialise the network
	if (ilt_dada_initialise_port(config) < 0) {
		return -1;
	}

	// If requested, initialise the ringbuffer now
	if (setup_io) {
		if (ilt_dada_setup_ringbuffer(config) < 0) {
			return -1;
		}
	}

	return 0;

}


/**
 * @brief      Verify the provided configuration matches the packets we're
 *             receiving
 *
 * @param      config  The configuration struct
 * @param[in]  flags   The recv flags
 *
 * @return     0 (success) / -1 (failure) / -2 (metadata mismatch) / -3 (all
 *             data is 0-valued)
 */
int ilt_dada_check_network(ilt_dada_config *config, int flags) {

	// Read the first packet in the queue into the buffer (peek so it can be consumed later if we're late)
	uint8_t buffer[MAX_UDP_LEN];
	ssize_t recvreturn;
	if ((recvreturn = recvfrom(config->sockfd, &buffer[0], MAX_UDP_LEN, MSG_PEEK | flags, NULL, NULL)) == -1) {
		fprintf(stderr, "ERROR: Unable to peek at first packet (errno %d, %s).", errno, strerror(errno));
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			fprintf(stderr, "ERROR: This was an indication that no packets were available to be consumed. Attempting to continue.\n");
			return -2;
		}
		return -1;
	}

	// Check the packet header state
	int returnVal = 0;
	if ((returnVal = ilt_dada_check_header(config, &buffer[0])) < 0) {
		return returnVal;
	}


	// Check that the packet doesn't only contain 0-values if requested
	lofar_source_bytes *source = (lofar_source_bytes*) &(buffer[1]);
	if (config->checkInitData) {
		int checkData = 0;
		// Number of data samples = number of time samples * (number of beamlets / width of sample)
		// 4-bt mode == bitMode = 2, divide by 2 to get number of chars
		// 8-bit, bitmode = 1, divide by 1
		// 16-bit, bitmode = 0, divide by 0.5
		int dataSamples = UDPNTIMESLICE * ((int) buffer[6] / (source->bitMode ? source->bitMode : 0.5));
		for (int idx = 16; idx < dataSamples; idx++) {
			if (buffer[idx] != 0) {
				checkData = 1;
				break;
			}
		}

		if (checkData == 0) {
			fprintf(stderr, "WARNING: First packet on port %d only contained 0-valued samples.", config->portNum);
			return -3;
		}
	}

	// Copy the clock bit into the struct
	config->obsClockBit = source->clockBit;
	printf("Packet clock bit state: %d\n", config->obsClockBit);
	// 16 + (61, 122, 244) * 16 * 4 / (0.5, 1, 2)
	// Max == 7824
	// Calculate the packet size
	config->packetSize = (int) (UDPHDRLEN + buffer[6] * buffer[7] * ((float) UDPNPOL / (source->bitMode ? source->bitMode : 0.5)));
	printf("Packet size: %d\n", config->packetSize);


	// Offset by 1 to account for the fact we will read this packet again (since we used MSG_PEEK)
	config->currentPacket = lofar_udp_time_beamformed_packno(*((unsigned int*) &(buffer[8])), *((unsigned int*) &(buffer[12])), source->clockBit) - 1; 

	config->state |= NETWORK_CHECKED;
	return 0;
}

/**
 * @brief      Sanity check the contents of the CEP packet header
 *
 * @param      config  The ilt_dada configuration struct
 * @param      buffer  The buffer with the CEP header
 *
 * @return     0: success, -1: failure
 */
int ilt_dada_check_header(ilt_dada_config *config, uint8_t* buffer) {
	// Sanity check the components of the CEP packet header
	lofar_source_bytes *source = (lofar_source_bytes*) &(buffer[1]);
	if (config->checkInitParameters) {

		// Check the error bit
		if (source->errorBit) {
			fprintf(stderr, "ERROR: First packet on port %d has the RSP error bit set.", config->portNum);
			return -1;
		}

		// Check the CEP header version
		if (buffer[0] != UDPCURVER) {
			fprintf(stderr, "ERROR: UDP version on port %d appears malformed (RSP Version is not 3, %d).\n", config->portNum, (uint8_t) buffer[0] < UDPCURVER);
			return -1;
		}

		// Check the unix time isn't absurd
		if (*((unsigned int *) &(buffer[8])) <  LFREPOCH) {
			fprintf(stderr, "ERROR: on port %d appears malformed (data timestamp before 2008, %d).\n", config->portNum, *((uint32_t *) &(buffer[8])));
			return -1;
		}


		// Check that the RSP sequence is constrained
		// TODO: This does not account for the variability in RSPMAXSEQ between the 200MHz/160MHz clocks
		if (*((unsigned int *) &(buffer[12])) > RSPMAXSEQ) {
			fprintf(stderr, "ERROR: RSP Sequence on port %d appears malformed (sequence higher than 200MHz clock maximum, %d).\n", config->portNum, *((uint32_t *) &(buffer[12])));
			return -1;
		}

		// Check that the beam count is constrained
		if (buffer[6] > UDPMAXBEAM) {
			fprintf(stderr, "ERROR: Number of beams on port %d appears malformed (more than %d beamlets on a port, %d).\n", config->portNum, UDPMAXBEAM, (uint8_t) buffer[6]);
			return -1;
		}

		// Check the number of time slices is UDPNTIMESLICE (16)
		if (buffer[7] != UDPNTIMESLICE) {
			fprintf(stderr, "ERROR: Number of time slices on port %d appears malformed (time slices are %d, not UDPNTIMESLICE).\n", config->portNum, (uint8_t) buffer[7]);
			return -1;
		}

		// Check that the clock and bitmodes are the expected values
		// No longer checked, using these as the source of the data
		// Might re-do this section in the future (TODO?)
		/*
		if (source->clockBit != config->obsClockBit && config->obsClockBit != (unsigned char) -1) {
			fprintf(stderr, "ERROR: RSP reports a different clock than expected on port %d (expected %d, got %d).", config->portNum, config->obsClockBit, source->clockBit);
			return -2;
		}
		if (source->bitMode != config->obsBitMode && config->obsBitMode != (unsigned char) -1) {
			fprintf(stderr, "ERROR: Bitmode mismatch on port %d (expected %d, got %d).", config->portNum, config->obsBitMode, source->bitMode);
			return -2;
		}
		*/

		// Make sure the padding values haven't been set
		if (source->padding0 != 0 || source->padding1 != 0) {
			fprintf(stderr, "ERROR: Padding bits were non-zero on port %d (%d, %d).", config->portNum, source->padding0, source->padding1);
			return -1;
		}
	}

	return 0;
}

/**
 * @brief      Connect to and destroy a ringbuffer on the given key
 *
 * @param[in]  key   The ringbuffer key
 *
 * @return     0: success, -1: failure.
 */
int ilt_dada_connect_and_destroy_ringbuffer(int key) {
	ipcio_t tmpIpc = IPCIO_INIT;
	
	if (ipcio_connect(&tmpIpc, key) < 0) {
		fprintf(stderr, "WARNING: Failed to connect to ringbuffer %d, we may be able to continue from here.\n", key);
		return 0;
	}

	if (ipcio_destroy(&tmpIpc) < 0) {
		fprintf(stderr, "ERROR: Failed to destroy ringbuffer %d, exiting.\n", key);
		return -1;
	}

	return 0;
}

/**
 * @brief      Setup the ringbuffer with the configuration struct parameters
 *
 * @param      config  The ilt_dada configuration struct
 *
 * @return     0: success, -1: failure
 */
int ilt_dada_setup_ringbuffer(ilt_dada_config *config) {
	// If the ringbuffer hasn't already been allocated,
	if (!(config->state & RINGBUFFER_READY)) {
		// Use the UPM writer API to setup the ringbuffer and header
		if (lofar_udp_io_write_setup(config->io, 0) < 0) {
			// If the process has failed but the forceStartup flag is set, destroy the existing ringbuffer and allocate our new one.
			// TODO: I re-wrote the UPM API here, this block (and the connect_and_destroy func may no longer be needed, just set the right struct variables
			if (config->forceStartup) {
				fprintf(stderr, "ERROR: Failed to attach to given ringbuffer %d, attempting to destroy given keys and then will try to connect again.\n", config->io->outputDadaKeys[0]);

				// Cleanup the state of the I/O struct
				FREE_NOT_NULL(config->io->dadaWriter[0].hdu->data_block);
				FREE_NOT_NULL(config->io->dadaWriter[0].hdu->header_block);
				lofar_udp_io_write_cleanup(config->io, 1);

				if (ilt_dada_connect_and_destroy_ringbuffer(config->io->outputDadaKeys[0]) < 0) {
					return -1;
				}
				
				if (ilt_dada_connect_and_destroy_ringbuffer(config->io->outputDadaKeys[0] + 1) < 0) {
					return -1;
				}

				if (lofar_udp_io_write_setup(config->io, 0) < 0) {
					fprintf(stderr, "ERROR: Failed to connect to ringbuffer %d after attempting to destroy existing buffer, exiting.\n", config->io->outputDadaKeys[0]);
					return -1;
				}

			} else {
				return -1;
			}
		}
		// Mark the process as successful
		config->state |= RINGBUFFER_READY;
	}

	return 0;
}



/**
 * @brief      The main setup + processing loop
 *
 * @param      config  The ilt_dada configuration struct
 *
 * @return     0: success, -1: failure
 */
int ilt_dada_operate(ilt_dada_config *config) {

	// Ensure the network has been initialised
	if (!(config->state & NETWORK_READY)) {
		fprintf(stderr, "ERROR: Network has not yet been initialised. Exiting.\n");
		return -1;
	}

	// Allocate the ringbuffer if it has not yet been allocated (lazy startup option).
	if (!(config->state & RINGBUFFER_READY)) {
		if (ilt_dada_setup_ringbuffer(config) < 0) {
			return -1;
		}
	}

	// Timeout apparently is relative to the socket being opened, so there's no point in using it here.
	// Leaving this in as I tried to implement it a second time while forgetting it didn't work.
	//static struct timespec timeout;
	//timeout.tv_sec = (int) config->portTimeout;
	//timeout.tv_nsec = (int) ((config->portTimeout - ((int) config->portTimeout) ) * 1e9);

	// Initialise a parameters struct for a new observation
	static ilt_dada_operate_params params = { 	.msgvec = NULL, 
												.iovecs = NULL, 
												.timeout = NULL, 
												.packetsSeen = 0,
												.packetsLastSeen = 0,
												.packetsExpected = 0,
												.packetsLastExpected = 0,
												.bytesWritten = 0
											};
	params.finalPacket = config->endPacket;
	*(config->params) = params;

	VERBOSE(printf("Prepare\n"));
	if (ilt_data_operate_prepare(config) < 0) {
		return -1;
	}

	VERBOSE(printf("Network\n"));
	if (ilt_dada_check_network(config, 0) < 0) {
		return -1;
	}

	// Warn the user if we are starting late
	if (config->currentPacket > config->startPacket) {
		fprintf(stderr, "WARNING: We are already past the observation start time on port %d.\n", config->portNum);
		// Update the current packet so that we reflect the missed data in the packet loss
		config->currentPacket = config->startPacket;
	} else {
		// Sleep until we're 2 second from the desired start time
		int sleepTime = (config->startPacket - config->currentPacket) / (clock160MHzPacketRate * (1 - config->obsClockBit) + clock200MHzPacketRate * config->obsClockBit);
		if (sleepTime > 2) {
			ilt_dada_sleep_multilog(sleepTime, config->io->dadaWriter[0].multilog);
		}
	}

	VERBOSE(printf("Loop\n"));
	// Read new data from the port until the observation ends
	if (ilt_dada_operate_loop(config) < 0) {
		return -1;
	}

	// Print debug information about the observing run
	printf("Observation completed. Cleaning up. Final summary:\n");
	ilt_dada_packet_comments(config->io->dadaWriter[0].multilog, config->portNum, config->currentPacket, config->startPacket, config->endPacket, config->params->packetsLastExpected, config->params->packetsLastSeen, config->params->packetsExpected, config->params->packetsSeen);

	// Clean exit
	return 0;
}



/*
	// https://man7.org/linux/man-pages/man2/recvmmsg.2.html
       int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    int flags, struct timespec *timeout);
*/


/**
 * @brief      The main loop of the recorder; receive packets and copy them to a
 *             PSRDADA ringbuffer
 *
 * @param      config  The recording configuration
 *
 * @return     0: Success, -1: Early Exit / Failure
 */
int ilt_dada_operate_loop(ilt_dada_config *config) {
	int readPackets, localLoops = 0;
	long finalPacketOffset = (config->packetsPerIteration - 1) * config->packetSize;
	long lastPacket;
	ssize_t writeBytes, writtenBytes;


	// If we're starting early or the buffer started filling early, consume data until we reach the starting packet
	if (config->currentPacket > config->startPacket) {
		// Past the start up time, begin the main loop immediately.
		printf("Late start-up resulted in %ld packets to be missed, starting now.\n", config->currentPacket - config->startPacket);

		// TODO: Make a decision, should thse be included in the stats or not?
		//config->startPacket = config->currentPacket;
	} else {
		printf("Starting warm-up...\n");
		while (config->currentPacket < config->startPacket) {
			readPackets = recvmmsg(config->sockfd, config->params->msgvec, config->packetsPerIteration, config->recvflags, config->params->timeout);
			lastPacket = lofar_udp_time_beamformed_packno(*((unsigned int *) &(config->params->packetBuffer[finalPacketOffset + 8])),
			                                              *((unsigned int *) &(config->params->packetBuffer[finalPacketOffset + 12])),
			                                              ((lofar_source_bytes *) &(config->params->packetBuffer[1]))->clockBit);

			if (lastPacket >= (config->startPacket - config->packetsPerIteration)) {
				// TODO: assumes no packets loss
				writeBytes = readPackets * config->packetSize;
				writtenBytes = ipcio_write(config->io->dadaWriter[0].hdu->data_block, &(config->params->packetBuffer[0]), writeBytes);

				if (writtenBytes < 0) {
					fprintf(stderr, "ERROR Port %d: Failed to write data to ringbuffer %d, exiting.\n", config->portNum, config->io->outputDadaKeys[0]);
				} else if (writtenBytes != writeBytes) {
					fprintf(stderr, "WARNING Port %d: Tried to write %ld bytes to buffer but only wrote %ld.\n", config->portNum, writeBytes, writtenBytes);
				}

				config->params->bytesWritten += writtenBytes;
				config->params->packetsSeen += readPackets;
				config->params->packetsExpected += lastPacket - config->currentPacket;
				config->params->packetsLastSeen += readPackets;
				config->params->packetsLastExpected += lastPacket - config->currentPacket;
			}

			config->currentPacket = lastPacket;
		}
		printf("Warmup summary for port %d:\n", config->portNum);
		#pragma omp task firstprivate(config)
		ilt_dada_packet_comments(config->io->dadaWriter[0].multilog, config->portNum, config->currentPacket, config->startPacket, config->endPacket, config->params->packetsLastExpected, config->params->packetsLastSeen, config->params->packetsExpected, config->params->packetsSeen);

		// Remove old stats before starting the observations
		config->params->bytesWritten = 0;
		config->params->packetsSeen = 0;
		config->params->packetsExpected = 0;
		config->params->packetsLastSeen = 0;
		config->params->packetsLastExpected = 0;
	}

	// Create a locale variables for packets per iteration, so we can reduce the number for the final step
	int packetsPerIteration = config->packetsPerIteration;

	printf("Observation beginning...\n");
	// While we still have data to record,
	while (config->currentPacket < config->params->finalPacket) {
		// Record the next N packets
		readPackets = recvmmsg(config->sockfd, config->params->msgvec, packetsPerIteration, config->recvflags, config->params->timeout);
		

		// Sanity check the amount that are read
		if (readPackets < 0) {
			fprintf(stderr, "ERROR: recvmmsg on port %d (errno %d: %s)\n", config->portNum, errno, strerror(errno));
			return -1;
		}
		if (readPackets != packetsPerIteration) {
			fprintf(stderr, "WARNING: recvmmsg on port %d received less packets than requested (expected,%d, recieved %d)\n", config->portNum, packetsPerIteration, readPackets);
		}

		finalPacketOffset = (readPackets - 1) * config->packetSize;

		// Check the packets for errors if requested
		if (config->checkParameters == CHECK_ALL_PACKETS) {
			for (int packetIdx = 0; packetIdx < (readPackets - 1); packetIdx++) {
				if (ilt_dada_check_header(config, (uint8_t*) &config->params->packetBuffer[packetIdx * config->packetSize]) < 0) {
					fprintf(stderr, "ERROR: packet %d/%d port header data corrupted on port %d, exiting.\n\n", packetIdx, readPackets, config->portNum);
					return -1;
				}
			}
		} else if (config->checkParameters == CHECK_FIRST_LAST) {
			int firstHeader = ilt_dada_check_header(config, (uint8_t*) &config->params->packetBuffer[0]);
			int lastHeader = ilt_dada_check_header(config, (uint8_t*) &config->params->packetBuffer[finalPacketOffset]);
			if (firstHeader < 0 || lastHeader < 0) {
				fprintf(stderr, "ERROR: port first or late header data corrupted on port %d (%d / %d), exiting.\n\n", config->portNum, firstHeader, lastHeader);
				return -1;
			}
		}

		// Get the last packet number
		lastPacket = lofar_udp_time_beamformed_packno(*((unsigned int*) &(config->params->packetBuffer[finalPacketOffset + 8])), *((unsigned int*) &(config->params->packetBuffer[finalPacketOffset + 12])), ((lofar_source_bytes*) &(config->params->packetBuffer[1]))->clockBit);

		// Calculate packet loss / misses / etc.
		config->params->packetsSeen += readPackets;
		config->params->packetsExpected += lastPacket - config->currentPacket;
		config->params->packetsLastSeen += readPackets;
		config->params->packetsLastExpected += lastPacket - config->currentPacket;

		// Write the raw packets to the ringbuffer
		writeBytes = readPackets * config->packetSize;
		writtenBytes = lofar_udp_io_write(config->io, 0, &(config->params->packetBuffer[0]), writeBytes);

		VERBOSE(printf("%ld, %ld, %ld\n", ipcio_tell(config->io->dadaWriter[0].hdu->data_block), ipcio_tell(config->io->dadaWriter[0].hdu->data_block) % config->packetSize, ipcio_tell(config->io->dadaWriter[0].hdu->data_block) / config->packetSize % 256));

		// Check that all the packets were written
		if (writtenBytes < 0) {
			fprintf(stderr, "ERROR Port %d: Failed to write data to ringbuffer %d, exiting.\n", config->portNum, config->io->outputDadaKeys[0]);
		} else if (writtenBytes != writeBytes) {
			fprintf(stderr, "WARNING Port %d: Tried to write %ld bytes to buffer but only wrote %ld.\n", config->portNum, writeBytes, writtenBytes);
		}
		config->params->bytesWritten += writtenBytes;


		config->currentPacket = lastPacket;

		localLoops++;
		if (localLoops > config->writesPerStatusLog) {
			localLoops = 0;
			#pragma omp task firstprivate(config)
			ilt_dada_packet_comments(config->io->dadaWriter[0].multilog, config->portNum, config->currentPacket, config->startPacket, config->endPacket, config->params->packetsLastExpected, config->params->packetsLastSeen, config->params->packetsExpected, config->params->packetsSeen);
			config->params->packetsLastSeen = 0;
			config->params->packetsLastExpected = 0;
		}


		/*  else if (config->currentPacket + packetsPerIteration  > config->params->finalPacket) {
			// Reduce the number of packets to read t the end of the current page
			long bufSize = (long) ipcbuf_get_bufsz((ipcbuf_t *) config->io->dadaWriter[0].ringbuffer);
			printf("Remainder: %ld (%ld)\n", ipcio_tell(config->io->dadaWriter[0].ringbuffer) % bufSize, (long) ((bufSize - (ipcio_tell(config->io->dadaWriter[0].ringbuffer) % bufSize)) / config->packetSize));
			packetsPerIteration = (long) ((bufSize - (ipcio_tell(config->io->dadaWriter[0].ringbuffer) % bufSize)) / config->packetSize) ?: config->packetsPerIteration;
			printf("%ld\n", packetsPerIteration);
		}
		*/
	}


	return 0;
}

/**
 * @brief      Setup the memory and structures needed to receive packets via
 *             recvmmsg
 *
 * @param      config  The recording configuration
 *
 * @return     0: Success, -1: Failure
 */
int ilt_data_operate_prepare(ilt_dada_config *config) {

	// Allocate memory for buffers
	config->params->packetBuffer = (int8_t*) calloc(config->packetsPerIteration, config->packetSize * sizeof(int8_t));
	config->params->msgvec = (struct mmsghdr*) calloc(config->packetsPerIteration, sizeof(struct mmsghdr));
	config->params->iovecs = (struct iovec*) calloc(config->packetsPerIteration, sizeof(struct iovec));

	if (config->params->packetBuffer == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate buffer for packetBuffer on port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
		return -1;
	}

	if (config->params->msgvec == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate buffer for msgvec on port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
		return -1;
	}


	if (config->params->iovecs == NULL) {
		fprintf(stderr, "ERROR: Failed to allocate buffer for iovecs on port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
		return -1;
	}

	// TODO: Investigate if it can be more efficient to set msg_iovlen / iov_len to higher values (haven't seen it used like that in any documentation)
	for (int i = 0; i < config->packetsPerIteration; i++) {
		// Don't target a specific receiver
		config->params->msgvec[i].msg_hdr.msg_name = NULL;
		// Point at the buffer for this packet
		config->params->msgvec[i].msg_hdr.msg_iov = &(config->params->iovecs[i]);
		// Only collect one packet
		config->params->msgvec[i].msg_hdr.msg_iovlen = 1;
		// Don't collect the UDP metadata
		config->params->msgvec[i].msg_hdr.msg_control = NULL;
		// Initialise the flag to 0
		config->params->msgvec[i].msg_hdr.msg_flags = 0;

		// Setup the packet buffer for this packet
		config->params->iovecs[i].iov_base = (void*) &(config->params->packetBuffer[i * config->packetSize]);
		config->params->iovecs[i].iov_len = config->packetSize;

	}

	return 0;
}

/**
 * @brief      Log information on packet loss, total observed packets
 *
 * @param      mlog                 The mlog
 * @param[in]  portNum              The port number
 * @param[in]  currentPacket        The current packet
 * @param[in]  startPacket          The start packet
 * @param[in]  endPacket            The end packet
 * @param[in]  packetsLastExpected  The packets last expected
 * @param[in]  packetsLastSeen      The packets last seen
 * @param[in]  packetsExpected      The packets expected
 * @param[in]  packetsSeen          The packets seen
 * @param      config  The recording configuration
 */
void ilt_dada_packet_comments(multilog_t *mlog, int portNum, long currentPacket, long startPacket, long endPacket, long packetsLastExpected, long packetsLastSeen, long packetsExpected, long packetsSeen) {
	const size_t maxlen = 2047;
	char messageBlock[6][maxlen + 1];

	snprintf(messageBlock[0], maxlen,"Port %d\tObservation %.1f%% Complete\t\t\tCurrent Packet %ld\n", portNum, 100.0f * (float) (currentPacket - startPacket) / (float) (endPacket - startPacket), currentPacket);
	snprintf(messageBlock[1], maxlen, "Packets\t\tExpected\t\tSeen\t\t\tMissed\n");
	snprintf(messageBlock[2], maxlen, "N (Current)\t%ld\t\t\t%ld\t\t\t%ld\n", packetsLastExpected, packetsLastSeen, packetsLastExpected - packetsLastSeen);
	snprintf(messageBlock[3], maxlen, "%% (Current)\t...\t\t\t%.1f\t\t\t%.1f\n", 100.0f * (float) (packetsLastSeen) / (float) (packetsLastExpected), 100.0f * (float) (packetsLastExpected - packetsLastSeen) / (float) (packetsLastExpected));
	snprintf(messageBlock[4], maxlen, "N (Total)\t%ld\t\t\t%ld\t\t\t%ld\n", packetsExpected, packetsSeen, packetsExpected - packetsSeen);
	snprintf(messageBlock[5], maxlen, "%% (Total)\t...\t\t\t%.1f\t\t\t%.1f\n", 100.0f * (float) (packetsSeen) / (float) (packetsExpected), 100.0f * (float) (packetsExpected - packetsSeen) / (float) (packetsExpected));
	multilog(mlog, 6, "%s%s%s%s%s%s", messageBlock[0], messageBlock[1], messageBlock[2], messageBlock[3], messageBlock[4], messageBlock[5]);
}






/**
 * @brief      Cleanup the sockets and memory used for the recorder and
 *             ringbuffer
 *
 * @param      config  The recording configuration
 */
void ilt_dada_config_cleanup(ilt_dada_config *config) {

	if (config->params != NULL) {
		FREE_NOT_NULL(config->params->packetBuffer);
		FREE_NOT_NULL(config->params->msgvec);
		FREE_NOT_NULL(config->params->iovecs);
		FREE_NOT_NULL(config->params->timeout);
		FREE_NOT_NULL(config->params);
	}
	lofar_udp_io_write_cleanup(config->io, 1);

	// Close the socket if it was successfully created
	if (config->sockfd != -1) {
		shutdown(config->sockfd, SHUT_RDWR);
	}

	FREE_NOT_NULL(config);
}

// Remove diagnostic ignored "openmp-use-default-none"
#pragma clang diagnostic pop