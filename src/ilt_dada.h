// Main includes
#ifndef __ILT_DADA_INCLUDE_H
#define __ILT_DADA_INCLUDE_H

// Include udpPacketManager general for consistant constants between writer/consumer
//#include "lofar_udp_general.h"

// Socket-related defines and includes (getaddrinfo man page requests all of them)
#define _GNU_SOURCE 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

// Let me print stuff and see errors
#include <stdio.h>
#include <errno.h>

// Include for sleep();
#include <unistd.h>

// Include for strerror();
#include <string.h>


// PSRDADA includes
#include "ipcbuf.h"

#endif

// Main Defines
#ifndef __ILT_DADA_DEFINES_H
#define __ILT_DADA_DEFINES_H

// Time to wait between attempts at an operation that can fail
#define SLEEPTIME
// Verify, not sure if preambles/checksum are included asfter recv.
#define UDP_HDR_SIZE 50
#define ETH_PKT_CKSM 4

#endif





// Main Prototypes
#ifndef __ILT_DADA_PROTOS_H
#define __ILT_DADA_PROTOS_H
// Allow C++ imports too
#ifdef __cplusplus
extern "C" {
#endif 

// Main functions
int ilt_dada_initialise_port(const int portNum, const long bufferSize);
int ilt_dada_initialise_ringbuffer();
ipcbuf_t* ilt_dada_initialise_ringbuffer_from_scratch(ilt_dada_config *config);
int ilt_dada_operate(int sockfd, ipcbuf_t *ringbuffer);

int ilt_dada_checkup(int sockfd);

// Internal functions
void cleanup_initialise_port(struct addrinfo *serverInfo, int sockfd_init);

#ifdef __cplusplus
}
#endif
#endif