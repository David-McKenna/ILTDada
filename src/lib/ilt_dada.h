// Main includes
#ifndef __ILT_DADA_INCLUDE_H
#define __ILT_DADA_INCLUDE_H

// Socket-related defines and includes (getaddrinfo man page requests all of them)
#define _GNU_SOURCE 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h> // struct timeval for timeout, recvmmsg has an edge case we'd like to avoid


// Let me print stuff and see errors
#include <stdio.h>
#include <errno.h>

// Include for sleep();
#include <unistd.h>

// Include for strerror();
#include <string.h>

// Include for free();
#include <stdlib.h>

// PSRDADA includes
#include "ipcio.h"
#include "multilog.h"

// Include udpPacketManager general for consistant constants between the writer/consumer, easy access to the source bytes struct and the packet nubmer functions
#include "lofar_udp_general.h"
#include "lofar_udp_misc.h"


#endif

// Main Defines
#ifndef __ILT_DADA_DEFINES_H
#define __ILT_DADA_DEFINES_H

#define MAX_UDP_LEN UDPNTIMESLICE * UDPNPOL * 122 + UDPHDRLEN

#endif

// If the compiler can't find this in the dada header
#ifndef DADA_DEFAULT_HEADER_SIZE
#define DADA_DEFAULT_HEADER_SIZE 4096
#endif


#ifndef __ILT_DADA_STRUCTS
#define __ILT_DADA_STRUCTS

typedef enum {
	NO_CHECKS,
	CHECK_ALL_PACKETS,
	CHECK_FIRST_LAST
} checkParameterTypes;

typedef struct ilt_dada_operate_params {
	char *packetBuffer;
	struct mmsghdr *msgvec;
	struct iovec *iovecs;
	struct timespec *timeout;
	
	long packetsSeen;
	long packetsExpected;
	long packetsLastSeen;
	long packetsLastExpected;
	long finalPacket;
	int minReads;
	long bytesWritten;
} ilt_dada_operate_params;
extern ilt_dada_operate_params ilt_dada_operate_params_default;

typedef struct ilt_dada_config {
	// UDP configuration
	int portNum;
	long portBufferSize;
	int portPriority;
	int packetSize;
	float portTimeout;
	int recvflags;

	// ILTDada runtime options
	int checkInitParameters;
	int checkInitData;
	int checkParameters;
	int cleanupTimeout;


	// Observation configuration
	long startPacket;
	long endPacket;
	int packetsPerIteration;
	unsigned char obsClockBit;
	unsigned char obsBitMode;


	// PSRSDADA configuration
	int key;
	uint64_t nbufs;
	uint64_t bufsz;
	unsigned int num_readers;
	char syslog;
	char programName[64];


	// Ringbuffer working variables
	int sockfd;
	char headerText[DADA_DEFAULT_HEADER_SIZE];
	long currentPacket;
	ipcio_t *ringbuffer;
	ipcio_t *header;
	multilog_t *multilog;

	// Main operation loop variables
	ilt_dada_operate_params *params;

} ilt_dada_config;
extern ilt_dada_config ilt_dada_config_default;
#endif


// Main Prototypes
#ifndef __ILT_DADA_PROTOS_H
#define __ILT_DADA_PROTOS_H
// Allow C++ imports too
#ifdef __cplusplus
extern "C" {
#endif 

// Main functions
int ilt_dada_initialise_port(ilt_dada_config *config);
int ilt_dada_initialise_ringbuffer(ilt_dada_config *config);
void ilt_dada_cleanup(ilt_dada_config *config);


int ilt_dada_check_config(ilt_dada_config *config);
int ilt_dada_check_network(ilt_dada_config *config);
int ilt_dada_check_header(ilt_dada_config *config, unsigned char* buffer);

int ilt_dada_operate(ilt_dada_config *config);
int ilt_dada_operate_loop(ilt_dada_config *config);
void ilt_dada_packet_comments(ilt_dada_config *config);



// Internal functions
void cleanup_initialise_port(struct addrinfo *serverInfo, int sockfd_init);
int ilt_dada_initialise_ringbuffer_hdu(ilt_dada_config *config);
int ilt_data_operate_prepare(ilt_dada_config *config);
void ilt_dada_operate_cleanup(ilt_dada_config *config);

#ifdef __cplusplus
}
#endif
#endif