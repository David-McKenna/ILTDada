
// Main includes
#include "ilt_dada.h"


// CLI includes
#ifndef __ILT_DADA_CLI_INCLUDE_H
#define __ILT_DADA_CLI_INCLUDE_H

#endif

// CLI Defines
#ifndef __ILT_DADA_CLI_DEFINES_H
#define __ILT_DADA_CLI_DEFINES_H

#define PACKET_DURATION_200 0.00008192
#define PACKeT_DURATION_160 0.0001024

#endif


// CLI Prototypes
#ifndef __ILT_DADA_CLI_PROTOS_H
#define __ILT_DADA_CLI_PROTOS_H
// Allow C++ imports too
#ifdef __cplusplus
extern "C" {
#endif 

int main(int argc, char  *argv[]);
int get_packet_rate(int clock_200);

#ifdef __cplusplus
}
#endif
#endif