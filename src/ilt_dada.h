// Main includes
#ifndef __ILT_DADA_INCLUDE_H
#define __ILT_DADA_INCLUDE_H

// Include udpPacketManager general for consistant constants between writer/consumer
#include "lofar_udp_general.h"

// Socket-related defines and includes
#define _GNU_SOURCE 
#include <sys/socket.h>

#endif

// Main Defines
#ifndef __ILT_DADA_DEFINES_H
#define __ILT_DADA_DEFINES_H

// Verify, not sure if preambles/checksum are included asfter recv.
#define UDP_HDR_SIZE 50
#define ETH_PKT_CKSM 4

#endif


#ifdef

#else



// Main Prototypes
#ifndef __ILT_DADA_PROTOS_H
#define __ILT_DADA_PROTOS_H
// Allow C++ imports too
#ifdef __cplusplus
extern "C" {
#endif 


#ifdef __cplusplus
}
#endif
#endif