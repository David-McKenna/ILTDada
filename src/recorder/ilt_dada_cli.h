
// Main includes
#include "ilt_dada.h"
#include "lofar_cli_meta.h"
#include "lofar_udp_general.h"


// CLI Prototypes
#ifndef __ILT_DADA_CLI_PROTOS_H
#define __ILT_DADA_CLI_PROTOS_H
// Allow C++ imports too
#ifdef __cplusplus
extern "C" {
#endif 

int main(int argc, char  *argv[]);
time_t unixTimeFromString(const char *inputStr);
int ilt_dada_cli_check_times(char *startTime, char *endTime, double obsSeconds, int ignoreTimeCheck, int minStartup);

#ifdef __cplusplus
}
#endif
#endif