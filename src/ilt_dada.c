#include "ilt_dada.h"



ilt_dada_config ilt_dada_default = {

	// UDP configuration
	.portNum = -1,
	.portBufferSize = -1,
	.portPriority = 6,


	// Startup configuration
	.checkObsParameters = 1,
	.checkObsData = 1,


	// Observation configuration
	.startPacket = -1,
	.obsClockBit= -1,
	.obsBitMode = -1,


	// PSRSDADA configuration
	.key = 0,
	.nbufs = 0,
	.bufsz = 0,
	.num_readers = 1,


	// PSRDADA working variables
	.sockfd = -1,
	.ringbuffer = NULL
};



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

	if (optVal < (2 * config->portBufferSize - 1)) {	
		if (setsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &(config->portBufferSize), sizeof(config->portBufferSize)) == -1) {
			fprintf(stderr, "ERROR: Failed to adjust buffer size on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
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
	// of our observation.
	const int allowReuse = 1;
	if (setsockopt(sockfd_init, SOL_SOCKET, SO_REUSEADDR, &allowReuse, sizeof(allowReuse)) == -1) {
		fprintf(stderr, "ERROR: Failed to set port re-use property on port %d (errno%d: %s).\n", config->portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

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





// http://psrdada.sourceforge.net/manuals/Specification.pdf

/**
 * @brief      Setup a ringbuffer from only ilt_dada_config
 *
 * @param      config  The ilt_dada configuration struct
 *
 * @return     0 (success) / -1 (failure)
 */
ipcbuf_t* ilt_dada_initialise_ringbuffer_from_scratch(ilt_dada_config *config) {
	// Initialise a ringbuffer struct
	static ipcbuf_t ringbuffer = IPCBUF_INIT;
	config->ringbuffer = &ringbuffer;

	// Initialise the ringbuffer, exit on failure
	if (ilt_dada_initialise_ringbuffer(&ringbuffer, config) < 0) {
		return NULL;
	}

	return &ringbuffer;

}

/**
 * @brief      Initialise a PSRDADA ringbuffer
 *
 * @param      ringbuffer  The ringbuffer struct
 * @param      config      The ringbuffer configuration
 *
 * @return     0 (success) / -1 (failure) / -2 (unrecoverable failure)
 */
int ilt_dada_initialise_ringbuffer(ipcbuf_t *ringbuffer, ilt_dada_config *config) {
	
	// Create the ringbuffer instance
	if (ipcbuf_create(ringbuffer, config->key, config->nbufs, config->bufsz, config->num_readers) < 0) {
		// ipcbuf_create(...) prints error to stderr, so we just need to exit.
		return -2;
	}


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
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_initial_checkup(ilt_dada_config *config) {

	unsigned char buffer[MAX_UDP_LEN];
	if (recvfrom(config->sockfd, &buffer[0], MAX_UDP_LEN, MSG_PEEK, NULL, NULL) == -1) {
		fprintf(stderr, "ERROR: Unable to peek at first packet (errno %d, %s).", errno, strerror(errno));
		return -2;
	}

	lofar_source_bytes *source = (lofar_source_bytes*) &(buffer[1]);
	if (source->errorBit) {
		fprintf(stderr, "ERROR: First packet on port %d has the RSP error bit set.", config->portNum);
		return -1;
	}

	if (config->checkObsParameters) {
		if (buffer[0] < UDPCURVER) {
			fprintf(stderr, "ERROR: UDP version on port %d appears malformed (RSP Version less than 3, %d).\n", config->portNum, (unsigned char) buffer[0] < UDPCURVER);
			return -1;
		}

		if (*((unsigned int *) &(buffer[8])) <  LFREPOCH) {
			fprintf(stderr, "ERROR: on port %d appears malformed (data timestamp before 2008, %d).\n", config->portNum, *((unsigned int *) &(buffer[8])));
			return -1;
		}

		if (*((unsigned int *) &(buffer[12])) > RSPMAXSEQ) {
			fprintf(stderr, "ERROR: RSP Sequence on port %d appears malformed (sequence higher than 200MHz clock maximum, %d).\n", config->portNum, *((unsigned int *) &(buffer[12])));
			return -1;
		}

		if (buffer[6] > UDPMAXBEAM) {
			fprintf(stderr, "ERROR: Number of beams on port %d appears malformed (more than %d beamlets on a port, %d).\n", config->portNum, UDPMAXBEAM, (unsigned char) buffer[6]);
			return -1;
		}

		if (buffer[7] != UDPNTIMESLICE) {
			fprintf(stderr, "ERROR: Number of time slices on port %d appears malformed (time slices are %d, not UDPNTIMESLICE).\n", config->portNum, (unsigned char) buffer[7]);
			return -1;
		}

		if (source->clockBit != config->obsClockBit && config->obsClockBit != (unsigned char) -1) {
			fprintf(stderr, "ERROR: RSP reports a different clock than expected on port %d (expected %d, got %d).", config->portNum, config->obsClockBit, source->clockBit);
			return -1;
		}
		if (source->bitMode != config->obsBitMode && config->obsBitMode != (unsigned char) -1) {
			fprintf(stderr, "ERROR: Bitmode mismatch on port %d (expected %d, got %d).", config->portNum, config->obsBitMode, source->bitMode);
			return -1;
		}

		if (source->padding0 != 0 || source->padding1 != 0) {
			fprintf(stderr, "ERROR: Padding bits were non-zero on port %d (%d, %d).", config->portNum, source->padding0, source->padding1);
			return -1;
		}
	}

	if (config->checkObsData) {
		int checkData = 0;
		// Number of data sampels = number of time samples * (number of beamlets / width of sample)
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
		}
	}

	return 0;
}



int ilt_dada_operate(ilt_dada_config *config) {

	/*

	while () {
		recvmmsg(...);

		ipcbuf_lock_write(ringbuffer);


		// do stuff/

		ipcbuf_unlock_write(ringbuffer);

	}

	*/
}


/*
	// https://man7.org/linux/man-pages/man2/recvmmsg.2.html
       int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    int flags, struct timespec *timeout);
*/



int main() {
	ilt_dada_config cfg = ilt_dada_default;
	cfg.portNum = 1234;
	cfg.sockfd = ilt_dada_initialise_port(&cfg);
	ilt_dada_initialise_ringbuffer_from_scratch(&cfg);
	ilt_dada_initial_checkup(&cfg);
	ipcbuf_destroy(cfg.ringbuffer);

}