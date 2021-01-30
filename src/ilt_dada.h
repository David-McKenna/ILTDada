// Main includes
#ifndef __ILT_DADA_INCLUDE_H
#define __ILT_DADA_INCLUDE_H

// Include udpPacketManager general for consistant constants between the writer/consumer and easy access to the source bytes struct
#include "lofar_udp_general.h"

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


// PSRDADA includes
#include "ipcbuf.h"
#include <stdint.h> // For uint64_t support

#endif

// Main Defines
#ifndef __ILT_DADA_DEFINES_H
#define __ILT_DADA_DEFINES_H

// Time to wait between attempts at an operation that can fail
#define SLEEPTIME
// Verify, not sure if preambles/checksum are included asfter recv.
#define UDP_HDR_SIZE 50
#define ETH_PKT_CKSM 4
#define MAX_UDP_LEN 7824

#endif


#ifndef __ILT_DADA_CONFIG_STRUCT
#define __ILT_DADA_CONFIG_STRUCT

typedef struct ilt_dada_config {
	// UDP configuration
	int portNum;
	long portBufferSize;
	int portPriority;
	int packetSize;

	// ILTDada runtime options
	int checkInitParameters;
	int checkInitData;
	int checkObsParameters;
	int checkObsData;


	// Observation configuration
	long startPacket;
	unsigned char obsClockBit;
	unsigned char obsBitMode;


	// PSRSDADA configuration
	int key;
	uint64_t nbufs;
	uint64_t bufsz;
	unsigned int num_readers;


	// Ringbuffer working variables
	int sockfd;
	char headerText[DADA_DEFAULT_HEADER_SIZE];
	long currentPacket#
	ipcbuf_t *ringbuffer;
	ipcbuf_t *header;

} ilt_dada_config;
extern ilt_dada_config ilt_dada_config_default;
#endif



#ifndef __ILT_DADA_INLINE_PACKETNO
#define __ILT_DADA_INLINE_PACKETNO
inline long beamformed_packno(unsigned int timestamp, unsigned int sequence, unsigned int clock200MHz) {
 	//VERBOSE(printf("Packetno: %d, %d, %d\n", timestamp, sequence, clock200MHz););
	return ((timestamp*1000000l*(160+40*clock200MHz)+512)/1024+sequence)/16;
}
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

ipcbuf_t* ilt_dada_initialise_ringbuffer_from_scratch(ilt_dada_config *config);
int ilt_dada_initialise_ringbuffer(ipcbuf_t *ringbuffer, ilt_dada_config *config);
int ilt_dada_initial_checkup(ilt_dada_config *config);

int ilt_dada_operate(ilt_dada_config *config);

int ilt_dada_cleanup(ilt_dada_config *config);


// Internal functions
void cleanup_initialise_port(struct addrinfo *serverInfo, int sockfd_init);
int ilt_data_operate_prepare_buffers();
void ilt_dada_operate_cleanup_buffers(char *packetBuffer, struct mmsghdr *msgvec, struct iovec *iovecs)

#ifdef __cplusplus
}
#endif
#endif