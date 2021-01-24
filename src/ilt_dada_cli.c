#include "ilt_dada_cli.h"


int main(int argc, char  *argv[]) {

}


int get_packet_rate(int clock_200) {
	if (clock_200) {
		return (int) 1 + (1f / PACKET_DUATION_200MHZ);
	} 

	return (int) 1 + (1f / PACKET_DURATION_160MHZ);
}