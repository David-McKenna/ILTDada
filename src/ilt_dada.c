#include "ilt_dada.h"
#include <limits.h>

// Future notes
// https://www.kernel.org/doc/Documentation/networking/packet_mmap.txt
// https://github.com/josephmartin09/packet_mmap/blob/master/packet_mmap.c


ilt_dada_operate_params ilt_dada_operate_params_default = {
	.packetBuffer = NULL,
	.msgvec = NULL,
	.iovecs = NULL,
	.timeout = NULL,
	
	.packetsSeen = 0,
	.packetsExpected = 0,
	.finalPacket = -1,
	.firstLoop = 1,
	.bytesWritten = 0
};

ilt_dada_config ilt_dada_config_default = {

	// UDP configuration
	.portNum = -1,
	.portBufferSize = -1,
	.portPriority = 6,
	.portTimeout = 30,
	.packetSize = MAX_UDP_LEN,
	.recvflags = 0,


	// Startup configuration
	.checkInitParameters = 1,
	.checkInitData = 1,
	.checkObsParameters = 1,
	.checkObsData = 1,
	.checkPackets = 1,

	// Observation configuration
	.startPacket = -1,
	.endPacket = LONG_MAX,
	.packetsPerIteration = 256, // ~0.67 seconds of data
	.obsClockBit= -1,
	.obsBitMode = -1,


	// PSRSDADA configuration
	.key = 0,
	.nbufs = 0,
	.bufsz = 0,
	.num_readers = 1,
	.syslog = 0,
	.programName = "ILTDada",


	// PSRDADA working variables
	.sockfd = -1,
	.headerText = "",
	.currentPacket = -1,
	.ringbuffer = NULL,
	.header = NULL,
	.multilog = NULL,
	.params = NULL
};

inline long beamformed_packno(unsigned int timestamp, unsigned int sequence, unsigned int clock200MHz) {
 	//VERBOSE(printf("Packetno: %d, %d, %d\n", timestamp, sequence, clock200MHz););
	return ((timestamp*1000000l*(160+40*clock200MHz)+512)/1024+sequence)/16;
}

/**
 * @brief      Initialise a UDP network docket on the gien port number and
 *             expand the kernel buffer to the given size (in bytes)
 *
 * @param      config  The configuration struct
 *
 * @return     sockfd (success) / -1 (failure) / -2 (unresolveable failure)
 */
int ilt_dada_initialise_port(ilt_dada_config *config) {
	// http://beej.us/guide/bgnet/pdf/bgnet_usl_c_1.pdf
	// >>> The place most people get stuck around here is what order to call 
	// >>> these things in. In that, the man pages are no use, as you’ve 
	// >>> probably discovered.
	// Can confirm. This document is a life saver.

	// Ensure we aren't trying to bind to a reserved port
	// Kernel recommends using ports between 1024 and 49151
	if (config->portNum < 1023 || config->portNum > 49152) {
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
	sprintf(portNumStr, "%d", config->portNum);

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
	if (bind(sockfd_init, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
		fprintf(stderr, "ERROR: Failed to bind to port %d (errno %d: %s).", config->portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

	// We have successfuly build and binded to a socket, let's tweak some of
	// it's parameters
	

	// Check if the port buffer is larger than the requested buffer size.
	// We willthen increase the buffer size if it is smaller than bufferSize
	//
	// getsockopt will return 2x the actual buffer size, as it includes extra
	// space to account for the kernel overheads, hense the need to double
	// bufferSize in this comparison
	//
	// https://linux.die.net/man/7/socket 
	// https://linux.die.net/man/2/setsockopt
	long optVal = 0;
	int optLen = sizeof(optVal);
	if (getsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &optVal, &optLen) == -1) {
		fprintf(stderr, "ERROR: Failed to get buffer size on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

	printf("Buffer: %ld, %ld, %ldMB\n", optVal, optVal<<20, config->portBufferSize);
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
			fprintf(stderr, "ERROR: Failed to fully adjust buffer size on port %d (attempted to set to %ld, call returned %ld).\n", config->portNum, config->portBufferSize * 2, optVal);
			FILE* rmemMax = fopen("/proc/sys/net/core/rmem_max", "r");
			if (rmemMax != NULL) {
				long rmemMaxVal;
				int dummy = fscanf(rmemMax, "%ld", &rmemMaxVal);
				if (rmemMaxVal < config->portBufferSize) {
					fprintf(stderr, "ERROR: This was because your kernel has the maximum UDP buffer size set to a lower value than you requested (%ld).\nERROR: Please increase the value stored in /proc/sys/net/core/rmem_max if you want to use a larger buffer.\n", rmemMaxVal);
				}
				fclose(rmemMax);
			}
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}
	}


	// Without root permisisons we can increase the port priority up to 6 
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

	// Set a hard cap on the timeout for recieving data from the socket. We can't fully
	// 	trust recvmmsg here due to a known bug where the N_packs - 1 packet may block
	// 	infinitely if it is never recieved
	// 	https://man7.org/linux/man-pages/man2/recvmmsg.2.html#bugs 
	// 	I wasn't able to confirm that this actually worked, and ran into other issues with timeouts
	// 	so this is comments out for now.
	//const struct timeval timeout =  { .tv_sec = (int) (config->portTimeout / 1 ), .tv_usec = (int) ((config->portTimeout - ((int) config->portTimeout)) * 1e6) };
	//if (setsockopt(sockfd_init, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
	//	fprintf(stderr, "ERROR: Failed to set timeout on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
	//	cleanup_initialise_port(serverInfo, sockfd_init);
	//	return -1;
	//}

	// Cleanup the addrinfo linked list before returning
	cleanup_initialise_port(serverInfo, -1);
	// Return the socket fd
	return sockfd_init;
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
	freeaddrinfo(serverInfo);

	// Close the socket if it was successfully created
	if (sockfd_init != -1) {
		shutdown(sockfd_init, SHUT_RDWR);
	}
}




// Ringbuffer References:
// http://psrdada.sourceforge.net/manuals/Specification.pdf (outdated and many examples no longer work)
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/dada_hdu.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/ipcio.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/multilog.h
// https://sourceforge.net/p/psrdada/code/ci/master/tree/src/ascii_header.h


/**
 * @brief      Sanity check parameters before they are used
 *
 * @param      config  The ilt_dada_config struct
 *
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_check_config(ilt_dada_config *config) {

	return 0;
}

/**
 * @brief      Setup a ringbuffer from only ilt_dada_config
 *
 * @param      config  The ilt_dada configuration struct
 *
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_initialise_ringbuffer(ilt_dada_config *config) {

	if (ilt_dada_check_config(config) < 0) {
		return -1;
	}

	if (config->ringbuffer != NULL) {
		fprintf(stderr, "ERROR: Port %d attempted to initialsie a ringbuffer when a ringbuffer was already initialised, exiting.\n", config->portNum);
		return -1;
	}

	if (config->header != NULL) {
		fprintf(stderr, "ERROR: Port %d attempted to initialsie a header ringbuffer when a header ringbuffer was already initialised, exiting.\n", config->portNum);
		return -1;
	}

	if (config->multilog != NULL) {
		fprintf(stderr, "ERROR: Port %d attempted to initialise a ringbuffer when a multilog instance was already initialised, exiting.\n", config->portNum);
		return -1;
	}

	// Initialise the ringbuffer/header structs
	static ipcio_t ringbuffer;
	static ipcio_t header;
	ringbuffer = IPCIO_INIT;
	header = IPCIO_INIT;
	config->ringbuffer = &ringbuffer;
	config->header = &header;

	// Initialise a multilog instance for logging
	config->multilog = multilog_open(config->programName, config->syslog);

	if (config->multilog == NULL) {
		fprintf(stderr, "ERROR: Failed to initlaise multilog struct on port %d, exiting.\n", config->portNum);
		return -1;
	}

	// Initialise the ringbuffer, exit on failure
	if (ilt_dada_initialise_ringbuffer_hdu(config) < 0) {
		return -1;
	}

	return 0;

}

/**
 * @brief      Initialise a PSRDADA ringbuffer
 *
 * @param      ringbuffer  The ringbuffer struct
 * @param      config      The ringbuffer configuration
 *
 * @return     0 (success) / -1 (failure) / -2 (unrecoverable failure)
 */
int ilt_dada_initialise_ringbuffer_hdu(ilt_dada_config *config) {
	
	// Create  and connect to the ringbuffer instance
	if (ipcio_create(config->ringbuffer, config->key, config->nbufs, config->bufsz, config->num_readers) < 0) {
		// ipcio_create(...) prints error to stderr, so we just need to exit.
		return -2;
	}

	// Create  and connect to the header buffer instance
	if (ipcio_create(config->header, config->key + 1, 1, DADA_DEFAULT_HEADER_SIZE, config->num_readers) < 0) {
		// ipcio_create(...) prints error to stderr, so we just need to exit.
		return -2;
	}

	// Open the ringbuffer instance as the primary writer
	if (ipcio_open(config->ringbuffer, 'W') < 0) {
		// ipcio_open(...) prints error to stderr, so we just need to exit.
		return -2;
	}

	// Open the header buffer instance as the primary writer
	if (ipcio_open(config->header, 'W') < 0) {
		// ipcio_open(...) prints error to stderr, so we just need to exit.
		return -2;
	}


	// Mark the data as being from beofre the true start of the observation
	// Disable for the time being, I ran into issues trying to mark SOD later
	/*
	ipcbuf_lock_write((ipcbuf_t *) config->ringbuffer);
	if (ipcbuf_disable_sod((ipcbuf_t *) config->ringbuffer) < 0) {
		// ipcbuf_disable_sod(...) prints rttot yo stderr, so we just need to exit.
		return -1;
	}
	ipcbuf_unlock_write((ipcbuf_t *)config->ringbuffer);
	*/

	// This now requires root permissions; skip for the moment.
	/*
	// Lock the ringbuffer into physical RAM to prevent it being swap'd out
	if (ipcbuf_lock(ringbuffer) < 0) {
		// ipcbuf_lock(...) prints error to stderr, but let's warn that this is lik
		return -1;
	}
	*/

	return 0;
}

/**
 * @brief      Verify the provided configuration matches the packets we're
 *             recieving
 *
 * @param[in]  sockfd  The socket file descriptor
 * @param      config  The configuration struct
 *
 * @return     0 (success) / -1 (failure) / -2 (metadata mismatch) / -3 (all data is 0-valued)
 */
int ilt_dada_check_network(ilt_dada_config *config) {

	// Read the first packet in the queue into the buffer
	unsigned char buffer[MAX_UDP_LEN];
	printf("Recv\n");
	if (recvfrom(config->sockfd, &buffer[0], MAX_UDP_LEN, MSG_PEEK, NULL, NULL) == -1) {
		fprintf(stderr, "ERROR: Unable to peek at first packet (errno %d, %s).", errno, strerror(errno));
		return -1;
	}
	printf("Peek'd\n");

	// Sanity check the components of the CEP packet header
	lofar_source_bytes *source = (lofar_source_bytes*) &(buffer[1]);
	if (config->checkInitParameters) {

		// Check the error bit
		if (source->errorBit) {
			fprintf(stderr, "ERROR: First packet on port %d has the RSP error bit set.", config->portNum);
			return -1;
		}

		// Check the CEP header version (maybe require == 3?)
		if (buffer[0] < UDPCURVER) {
			fprintf(stderr, "ERROR: UDP version on port %d appears malformed (RSP Version less than 3, %d).\n", config->portNum, (unsigned char) buffer[0] < UDPCURVER);
			return -1;
		}

		// Check the unix time isn't absurd
		if (*((unsigned int *) &(buffer[8])) <  LFREPOCH) {
			fprintf(stderr, "ERROR: on port %d appears malformed (data timestamp before 2008, %d).\n", config->portNum, *((unsigned int *) &(buffer[8])));
			return -1;
		}


		// Check that the RSP sequence is constrained
		// This does not account for the variability in RSPMAXSEQ between the 200MHz/160MHz clocks
		if (*((unsigned int *) &(buffer[12])) > RSPMAXSEQ) {
			fprintf(stderr, "ERROR: RSP Sequence on port %d appears malformed (sequence higher than 200MHz clock maximum, %d).\n", config->portNum, *((unsigned int *) &(buffer[12])));
			return -1;
		}

		// Check that the beam count is constrained
		if (buffer[6] > UDPMAXBEAM) {
			fprintf(stderr, "ERROR: Number of beams on port %d appears malformed (more than %d beamlets on a port, %d).\n", config->portNum, UDPMAXBEAM, (unsigned char) buffer[6]);
			return -1;
		}

		// Check the number of time slices is UDPNTIMESLICE (16)
		if (buffer[7] != UDPNTIMESLICE) {
			fprintf(stderr, "ERROR: Number of time slices on port %d appears malformed (time slices are %d, not UDPNTIMESLICE).\n", config->portNum, (unsigned char) buffer[7]);
			return -1;
		}

		// Check that the clock and bitmodes are the expected values
		if (source->clockBit != config->obsClockBit && config->obsClockBit != (unsigned char) -1) {
			fprintf(stderr, "ERROR: RSP reports a different clock than expected on port %d (expected %d, got %d).", config->portNum, config->obsClockBit, source->clockBit);
			return -2;
		}
		if (source->bitMode != config->obsBitMode && config->obsBitMode != (unsigned char) -1) {
			fprintf(stderr, "ERROR: Bitmode mismatch on port %d (expected %d, got %d).", config->portNum, config->obsBitMode, source->bitMode);
			return -2;
		}

		// Make sure the padding values haven't been set
		if (source->padding0 != 0 || source->padding1 != 0) {
			fprintf(stderr, "ERROR: Padding bits were non-zero on port %d (%d, %d).", config->portNum, source->padding0, source->padding1);
			return -1;
		}
	}


	// Check that the packet doesn't only contain 0-values
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
			fprintf(stderr, "WARNING: First packet on port 0 only contained 0-valued samples.");
			return -3;
		}
	}

	config->currentPacket = beamformed_packno(*((unsigned int*) &(buffer[8])), *((unsigned int*) &(buffer[12])), source->clockBit); 

	return 0;
}



int ilt_dada_operate(ilt_dada_config *config) {

	// Timeout apparently is relative to the socket being oepned, so it's useless.
	//static struct timespec timeout;
	//timeout.tv_sec = (int) config->portTimeout;
	//timeout.tv_nsec = (int) ((config->portTimeout - ((int) config->portTimeout) ) * 1e9);
	static ilt_dada_operate_params params = { 	.msgvec = NULL, 
												.iovecs = NULL, 
												.timeout = NULL, 
												.packetsSeen = 0,
												.packetsExpected = 0,
												.firstLoop = 1,
												.bytesWritten = 0
											};
	params.finalPacket = config->startPacket;

	config->params = &params;

	if (ilt_data_operate_prepare(config) < 0) {
		return -1;
	} 


	// Consume first packet
	if (recvfrom(config->sockfd, &(config->params->packetBuffer[0]), MAX_UDP_LEN, MSG_PEEK, NULL, NULL) == -1) {
		fprintf(stderr, "ERROR: Unable to peek at first packet for main operation (errno %d, %s).", errno, strerror(errno));
		ilt_dada_operate_cleanup(config);
		return -2;
	}

	if (ilt_dada_check_network(config) < 0) {
		ilt_dada_operate_cleanup(config);
		return -1;
	}

	printf("Current packet no.: %ld\n", config->currentPacket);
	long finalPacketOffset = (config->packetsPerIteration - 1) * config->packetSize;
	config->params->finalPacket = config->startPacket;

	// TEMP
	config->endPacket += config->currentPacket;




	// If we haven't passed the start of the observation, loop until we start
	if (config->currentPacket > config->startPacket) {
		fprintf(stderr, "WARNING: We are already past the observation start time on port %d.\n", config->portNum);
	} else if (ilt_dada_operate_loop(config) < 0) {
		ilt_dada_operate_cleanup(config);
		return -1;
	}

	// Print debug information about the startup period
	ilt_dada_packet_comments(config);


	// Mark the data as ready to be consumed
	//if (ipcio_start(config->ringbuffer, config->params->bytesWritten) < 0) {
	//return -1;
	//}

	// Reset loop variables for main observation
	config->params->finalPacket = config->endPacket;
	config->params->packetsSeen = 0;
	config->params->packetsExpected = 0;

	// Read new data from the until the observation ends
	if (ilt_dada_operate_loop(config) < 0) {
		ilt_dada_operate_cleanup(config);
		return -1;
	}

	// Print debug information about the observing run
	ilt_dada_packet_comments(config);

	// Mark the data as completed
	if (ipcio_stop(config->ringbuffer) < 0) {
		fprintf(stderr, "ERROR: Failed to mark end of data on port %d, exiting.\n", config->portNum);
		ilt_dada_operate_cleanup(config);
		return -1;
	}

	// Cleanup buffers
	ilt_dada_operate_cleanup(config);

	// Clean exit
	return 0;
}

int ilt_dada_operate_loop(ilt_dada_config *config) {
	char *bufferPointer;
	int readPackets;
	long finalPacketOffset = (config->packetsPerIteration - 1) * config->packetSize;
	long lastPacket;
	size_t writeBytes, writtenBytes;


	while (config->currentPacket < config->params->finalPacket || config->params->firstLoop == 1) {
		printf("Entering main loop: %ld, %ld, %d\n", config->currentPacket, config->params->finalPacket, config->params->firstLoop);
		readPackets = recvmmsg(config->sockfd, config->params->msgvec, config->packetsPerIteration, config->recvflags, config->params->timeout);
		
		if (readPackets < 0) {
			fprintf(stderr, "ERROR: recvmmsg on port %d (errno %d: %s)\n", config->portNum, errno, strerror(errno));
			return -1;
		}
		if (readPackets != config->packetsPerIteration) {
			fprintf(stderr, "ERROR: recvmmsg on port %d received less packets than requested (expected,%d, recieved %d)\n", config->portNum, config->packetsPerIteration, readPackets);
		}


		if (config->checkPackets) {
			for (int packetIdx = 0; packetIdx < config->packetsPerIteration; packetIdx++) {
				// Sanity check packet contents / flags?
			}
		}

	
		writeBytes = readPackets * config->packetSize;
		writtenBytes = ipcio_write(config->ringbuffer, &(config->params->packetBuffer[0]), writeBytes);

		if (writtenBytes != writeBytes) {
			fprintf(stderr, "WARNING Port %d: Tried to write %ld bytes to buffer but only wrote %ld.\n", config->portNum, writeBytes, writtenBytes);
		}
		config->params->bytesWritten += writtenBytes;


		lastPacket = beamformed_packno(*((unsigned int*) &(config->params->packetBuffer[finalPacketOffset + 8])), *((unsigned int*) &(config->params->packetBuffer[finalPacketOffset + 12])), ((lofar_source_bytes*) &(config->params->packetBuffer[1]))->clockBit);

		// Calcualte packet loss / misses / etc.
		config->params->packetsSeen += readPackets;
		config->params->packetsExpected += lastPacket - config->currentPacket;

		config->currentPacket = lastPacket;
		config->params->firstLoop = 0;
	}

	printf("Exit main loop\n");
	return 0;
}

int ilt_data_operate_prepare(ilt_dada_config *config) {

	// Allocate memory for buffers
	config->params->packetBuffer = (char*) calloc(config->packetsPerIteration, config->packetSize * sizeof(char));
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

	for (int i = 0; i < config->packetsPerIteration; i++) {
		// Don't target a specific reciever
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

void ilt_dada_operate_cleanup(ilt_dada_config *config) {
	free(config->params->packetBuffer);
	free(config->params->msgvec);
	free(config->params->iovecs);
}

void ilt_dada_packet_comments(ilt_dada_config *config) {
	;
}


/*
	// https://man7.org/linux/man-pages/man2/recvmmsg.2.html
       int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    int flags, struct timespec *timeout);
*/



int ilt_dada_cleanup(ilt_dada_config *config) {


	// Close the socket if it was successfully created
	if (config->sockfd != -1) {
		shutdown(config->sockfd, SHUT_RDWR);
	}


	// Wait for readers to finish up and exit, or timeout
	float totalSleep = 0.0;
	while (totalSleep < config->cleanupTimeout) {
		if (ipcbuf_get_reader_conn((ipcbuf_t *) config->ringbuffer) == 0) {
			break;
		}

		sleep(0.1);
	}

	// Close, disconnect and destroy the ringbuffer
	ipcio_close(config->ringbuffer);
	ipcio_close(config->header);

	ipcio_destroy(config->ringbuffer);
	ipcio_destroy(config->header);
}
