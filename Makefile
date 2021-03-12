# Don't default to sh/dash
SHELL = bash

# Library versions
LIB_VER = 0.1
LIB_VER_MINOR = 0
CLI_VER = 0.1


OPT_ARCH ?= "native"
CFLAGS += -W -Wall -Ofast -march=$(OPT_ARCH) -mtune=$(OPT_ARCH) -fPIC -funswitch-loops
#CFLAGS += -g -fsanitize=address


DEFINES += -DVERSION=$(LIB_VER) -DVERSION_MINOR=$(LIB_VER_MINOR) -DVERSIONCLI=$(CLI_VER)
CFLAGS += $(DEFINES)

LFLAGS 	+= -I./src -lpsrdada #-lefence

# Define our general build targets
OBJECTS = src/ilt_dada.o
CLI_OBJECTS = $(OBJECTS) src/ilt_dada_cli.o
TEST_CLI_OBJECTS = $(OBJECTS) src/ilt_dada_fill_buffer.o

PREFIX ?= /usr/local

# C -> CC
%.o: %.c
	$(CC) -c $(CFLAGS) -o ./$@ $< $(LFLAGS)

# CLI -> link with C++
all: cli test-cli

cli: $(CLI_OBJECTS)
	$(CC) $(CFLAGS) $(CLI_OBJECTS) -o ./ilt_dada $(LFLAGS)

test-cli: $(TEST_CLI_OBJECTS) 
	$(CC) $(CFLAGS) $(TEST_CLI_OBJECTS)  -o ./ilt_dada_fill_buffer $(LFLAGS)


# Install CLI, headers, library
install: cli test-cli
	mkdir -p $(PREFIX)/bin/ && mkdir -p $(PREFIX)/include/
	cp ./ilt_dada_fill_buffer $(PREFIX)/bin/
	cp ./ilt_dada $(PREFIX)/bin/
	cp ./src/*.h $(PREFIX)/include/


# Remove local build arifacts
clean:
	-rm ./src/*.o
	-rm ./ilt_dada_fill_buffer
	-rm ./ilt_dada

# Uninstall the software from the system
remove:
	rm $(PREFIX)/bin/ilt_dada_fill_buffer
	rm $(PREFIX)/bin/ilt_dada_fill_buffer
	cd src/; find . -name "*.h" -exec rm $(PREFIX)/include/{} \;
	make clean



