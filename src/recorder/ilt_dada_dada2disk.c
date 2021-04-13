#include "ilt_dada.h"
#include "lofar_cli_meta.h"
#include "lofar_udp_io.h"


void CLICleanup() {

}

void helpMessages() {

}

int main(int argc, char  *argv[]) {
	
	lofar_udp_config *config = calloc(1, sizeof(lofar_udp_config));
	(*config) = lofar_udp_config_default;

	lofar_udp_meta *meta = calloc(1, sizeof(lofar_udp_meta));
	(*meta) = lofar_udp_meta_default;
	meta->packetsPerIteration = 4096;

	lofar_udp_reader_input *rdrConfig = calloc(1, sizeof(lofar_udp_reader_input));
	(*rdrConfig) = lofar_udp_reader_input_default;

	lofar_udp_io_write_config *wrtConfig = calloc(1, sizeof(lofar_udp_io_write_config));
	(*wrtConfig) = lofar_udp_io_write_config_default;

	char tmpHeaders[MAX_NUM_PORTS][UDPHDRLEN] = { { 0 } };
	char *rawData[MAX_NUM_PORTS] = { NULL };
	char inputOpt;

	int inputProvided = 0, outputProvided = 0, ringbufferOffset = 10, parsed;
	long charsPerRead[MAX_NUM_PORTS] = { 0 };
	while((inputOpt = getopt(argc, argv, "i:o:n:m:f")) != -1) {
		switch (inputOpt) {
			case 'i':
				parsed = sscanf(optarg, "%d,%d", &(config->dadaKeys[0]), &ringbufferOffset);
				if (parsed < 1) {
					fprintf(stderr, "ERROR: Failed to parse input ringbuffer / offset (sscanf returned %d), exiting.\n", parsed);
					return 1;
				}
				inputProvided = 1;
				break;

			case 'o':
				if (lofar_udp_io_write_parse_optarg(wrtConfig, optarg) < 0) {
					helpMessages();
					return 1;
				}
				outputProvided = 1;
				break;

			case 'n':
				meta->numPorts = atoi(optarg);
				break;

			case 'm':
				meta->packetsPerIteration = atol(optarg);
				break;

			case 'f':
				wrtConfig->appendExisting = 1;
				break;


		}
	}

	if (!inputProvided) {
		fprintf(stderr, "ERROR: No input provided, exiting.\n");
		CLICleanup();
		return 1;
	}

	if (!outputProvided) {
		fprintf(stderr, "ERROR: No output provided, exiting.\n");
		CLICleanup();
		return 1;
	}


	if (ringbufferOffset == 1 || ringbufferOffset == -1) {
		fprintf(stderr, "ERROR: Ringubffer offset set to %d, but this will result in overlapping ringbuffers and DADA headers, exiting.\n", ringbufferOffset);
		return 1;
	}

	// Read in headers from the input
	for (int port = 0; port < meta->numPorts; port++) {
		config->dadaKeys[port] = config->dadaKeys[0] + port * ringbufferOffset;
		if (lofar_udp_io_fread_temp_DADA(tmpHeaders[port], sizeof(char), UDPHDRLEN, config->dadaKeys[port], 1) < UDPHDRLEN) {
			CLICleanup();
			return 1;
		}
	}

	// Extract metadata from the headers
	int emtpyBeamlets[2] = { 0 };
	if (lofar_udp_parse_headers(meta, tmpHeaders, emtpyBeamlets) < 0) {
		CLICleanup();
		return 1;
	}

	for (int port = 0; port < meta->numPorts; port++) {
		rawData[port] = calloc(meta->packetsPerIteration * meta->portPacketLength[port], sizeof(char));
		charsPerRead[port] = meta->packetsPerIteration * meta->portPacketLength[port] * (long) sizeof(char);
		if (rawData[port] == NULL) {
			fprintf(stderr, "ERROR: Failed to allocate %ld bytes for buffer %d, exiting.\n", meta->packetsPerIteration * meta->portPacketLength[port] * sizeof(char), port);
			CLICleanup();
			return 1;
		}
	}

	// Setup the readers
	rdrConfig->readerType = DADA_ACTIVE;
	for (int port = 0; port < meta->numPorts; port++) {
		if (lofar_udp_io_read_setup(rdrConfig, config, meta, port) < 0) {
			CLICleanup();
			return 1;
		}
	}

	// Setup the writers
	for (int outp = 0; outp < meta->numPorts; outp++) {
		if (lofar_udp_io_write_setup(wrtConfig, meta, 0) < 0) {
			CLICleanup();
			return 1;
		}
	}

	// Loop until we exit
	long dataRead, dataWrite;
	int exit = 0;
	while (!exit) {
		for (int port = 0; port < meta->numPorts; port++) {
			if ((dataRead = lofar_udp_io_read_DADA(rdrConfig, port, rawData[port], charsPerRead[port]) < charsPerRead[port]) ) {
				fprintf(stderr, "WARNING: Recieved less data than expected on port %d (%ld vs %ld)\n", port, charsPerRead[port], dataRead);
			}

			if (dataRead < 0) {
				fprintf(stderr, "NOTICE: No data returned for port %d, exiting.\n", port);
				exit = 1;
				break;
			}

			if ((dataWrite = lofar_udp_io_write(wrtConfig, port, rawData[port], dataRead)) != dataRead) {
				fprintf(stderr, "WARNING: Wrote less data than expected on port %d (%ld vs %ld)\n", port, dataRead, dataWrite);
			}

			if (dataWrite < 0) {
				fprintf(stderr, "ERROR: No data written for port %d, exiting.n", port);
			}
		}

	}


	for (int port = 0; port < meta->numPorts; port++ ) {
		lofar_udp_io_read_cleanup(rdrConfig, port);
		lofar_udp_io_write_cleanup(wrtConfig, port, 1); 
	}
	CLICleanup();
	return 0;

}