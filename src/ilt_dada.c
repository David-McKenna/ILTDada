#include "ilt_dada.h"



struct ilt_dada_config = {
	.key = 0,
	.nbufs = 0,
	.bufsz = 0
}



/**
 * @brief      Initialise a UDP network docket on the gien port number and
 *             expand the kernel buffer to the given size (in bytes)
 *
 * @param[in]  portNum     The UDP port number
 * @param[in]  bufferSize  The UDP socket buffer size
 *
 * @return     sockfd (success) / -1 (failure)
 */
int ilt_dada_initialise_port(const int portNum, const long bufferSize) {
	// http://beej.us/guide/bgnet/pdf/bgnet_usl_c_1.pdf
	// >>> The place most people get stuck around here is what order to call 
	// >>> these things in. In that, the man pages are no use, as you’ve 
	// >>> probably discovered.
	// Can confirm. This document is a life saver.


	// Bind to an IPv4 or IPv6 connection, using UDP packet paradigm, on a
	// wildcard address
	struct addrinfo addressInfo = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE
	};

	// Convert the port to a string for getaddrinfo
	char portNumStr[16];
	sprintf(portNumStr, "%d", portNum);

	// Struct to collect the results from getaddrinfo
	struct addrinfo *serverInfo;
	int status;

	// Populate the remaining parts of addressInfo
	if ((status = getaddrinfo(NULL, portNumStr, &addressInfo, &serverInfo)) < 0) {
		fprintf(stderr, "ERROR: Failed to get address info on port %d (errno %d: %s).", portNum, status, gai_strerror(status));
		return -1;
	}


	// Build our socket from the results of getaddrinfo
	int sockfd_init = -1;
	if ((sockfd_init = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_flags)) == -1) {
		fprintf(stderr, "ERROR: Failed to build socket on port %d (errno %d: %s).", portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

	// Attempt to bind to the socket
	if (bind(sockfd_init, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
		fprintf(stderr, "ERROR: Failed to bind to port %d (errno %d: %s).", portNum, errno, strerror(errno));
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
		fprintf(stderr, "ERROR: Failed to get buffer size on port %d (errno%d: %s).\n", portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

	if (optVal < (2 * bufferSize - 1)) {	
		if (setsockopt(sockfd_init, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize)) == -1) {
			fprintf(stderr, "ERROR: Failed to adjust buffer size on port %d (errno%d: %s).\n", portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}
	}


	// Without root permisisons we can increase the port priority up to 6 
	// If we are below the value set by portPriority, adjust the port priority 
	// to the given value.
	const int portPriority = 6;
	if (getsockopt(sockfd_init, SOL_SOCKET, SO_PRIORITY, &optVal, &optLen) == -1) {
		fprintf(stderr, "ERROR: Failed to get port priority on port %d (errno%d: %s).\n", portNum, errno, strerror(errno));
		cleanup_initialise_port(serverInfo, sockfd_init);
		return -1;
	}

	if (optVal < portPriority) {
		if (setsockopt(sockfd_init, SOL_SOCKET, SO_PRIORITY, &portPriority, sizeof(portPriority)) == -1) {
			fprintf(stderr, "ERROR: Failed to adjust port priority on port %d (errno%d: %s).\n", portNum, errno, strerror(errno));
			cleanup_initialise_port(serverInfo, sockfd_init);
			return -1;
		}
	}


	// Allow the port to be re-used, encase we are slow to cleanup after the end
	// of our observation.
	const int allowReuse = 1;
	if (setsockopt(sockfd_init, SOL_SOCKET, SO_REUSEADDR, &allowReuse, sizeof(allowReuse)) == -1) {
		fprintf(stderr, "ERROR: Failed to set port re-use property on port %d (errno%d: %s).\n", portNum, errno, strerror(errno));
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
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_initialise_ringbuffer(ipcbuf_t *ringbuffer, ilt_dada_config *config) {
	
	// Create the ringbuffer instance
	if (ipcbuf_create(ringbuffer, config->key, config->nbufs, config->bufsz) < 0) {
		// ipcbuf_create(...) prints error to stderr, so we just need to exit.
		return -1;
	}

	// Lock the ringbuffer into physical RAM to prevent it being swapp'd out
	if (ipcbuf_lock_shm(ringbuffer) < 0) {
		// ipcbuf_lock_shm(...) prints error to stderr, so we just need to exit.
		return -1;
	}

	return 0;
}



/**
 * @brief      Verify the configuration matches the packets we're recieving
 *
 * @param[in]  sockfd  The socket file descriptor
 *
 * @return     0 (success) / -1 (failure)
 */
int ilt_dada_checkup(int sockfd) {

}


int ilt_dada_operate(int sockfd, ipcbuf_t *ringbuffer) {


	while () {
		recvmmsg(...);

		ipcbuf_lock_write(ringbuffer);


		// do stuff/

		ipcbuf_unlock_write(ringbuffer);

	}
}


/*
	// https://man7.org/linux/man-pages/man2/recvmmsg.2.html
       int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    int flags, struct timespec *timeout);
*/

