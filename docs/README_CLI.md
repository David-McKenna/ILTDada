ILTDada CLI
===========
The ILTDada CLI controls the UDP networking component of the ILTDada library and the PSRDADA writer found in (udpPacketManager)[https://github.com/David-McKenna/udpPacketManager] to act as a ringbuffer recorder.

The resulting PSRDADA stream can be written to disk with the included `ilt_dada_dada2disk` CLI (described here)[README_dada2disk.md], or consumed as an input to udpPacketManager to perform online data processing. The ILTdada/udpPacketManager online processing setup has been tested on two main nodes,
- The I-LOFAR REALTA system, with dual 16 core Intel(R) Xeon(R) Gold 6130 CPU @ 2.10GHz, which can process all 4 ports of data from I-LOFAR without issues
- The LOFAR4SW Test node, with a 2 core Intel(R) Core(TM) i3-3220 CPU @ 3.30GHz, which can process both ports of data from the LOFAR4SW test array without issues

While testing, the main bottleneck has been found to be disks during the I/O stage, rather than CPU resources. More comprehensive benchmarks and resource utilisation will be conducted and provided here in the future.

Oddities
--------
While PSRDADA has the option to send a signal marking the end of observation to a ringbuffer, I have no been able to get it to wrk. As a result, ringbuffer many hang on the last read. The current workaround for this is to kill the ringbuffer if the reader has not progressed for over 5 seconds during the shutdown phase. As a result, it is recommended to keep a minimum gap of 15 seconds between the end / start of two consecutive observations.

Example Command
---------------
```shell
ilt_dada -S 2021-06-17T12:45:00 \  # The start time of an observation in UTC, ISOT format 
         -T 2021-06-17T20:20:00 \  # The end time of an observation in UTC, ISOT format
         -p 16080 \ # The UDP port to listen on
         -k 16080 \ # The ringbuffer to attach to / create for the output
         -n 256   \ # The number of packets to read for every network request
         -m 16    \ # The number of blocks of packet reads to store in each ringbuffer page 
         -s 10    \ # The maximum amount of time/data, in seconds, the ringbuffer can contain at any given time 
         -r 1     \ # The number of consumers that will read from the ringbuffer

```

Arguments
---------

#### -S (UTC ISOT string, YYYY-MM-DDTHH:MM:SS):
- The start time for the observation
- If this is in the past, the observation will start immediately but include the missed packets in any calculated dropped/missed packet statistics


#### -T (UTC ISOT string, YYYY-MM-DDTHH:MM:SS):
- The end time for the observation


#### -t (float):
- Instead of providing `-T`, `-t` can be provided to setthe observation length in seconds from the starting time.


#### -w (float):
- The minimum start-up time for the recorder
- The minimum amount of time to leave between initialising the recorder, networking components and ringbuffer before starting to observe packets
- When set to 5 seconds, this means that the process will sleep until 5 seconds before the start time, and only perform the major start-up components after it wakes up


#### -p (int):
- Input UDP socket/port number
- This depends on your station (I-LOFAR, for example, uses 16130 - 16133), but values between 1024 and 49151 to not conflict with system sockets.

#### -k (int):
- Output PSRDADA ringbuffer ID
- The given number, and the value +1, will be allocated as a pair of PSRDADA header and data ringbuffers
- This means if you chose to place the ringbuffer at 16130, ringbuffers at 16130 and 16131 will be allocated
- Any valid integer should be usable as the ringbuffer, though some low values may be used by system processes
- At I-LOFAR, we have chosen to to use 16130, 16140, 16150 and 16160 as our ringbuffers. I.e., the base port, plus 10 for every port beyond that
- In the case that you have a zombie ringbuffer you wish to kill with `dada_db`, you will need to convert this value into hex to select the correct ringbuffer


#### -n (int, recommended: 256):
- The number of packets to receive on the network socket for every iteration
- We recommend keeping this value to be a power of two, with values between 64 and 512 working well
- Both higher values and lower values may lead to packet loss, 256 has been a good value from experience


#### -m (int, recommended: 16 - 256):
- The number of batches of packets to store in a single ringbuffer block
- While it would be optimal to store all of the data in their own blocks, that is inefficient can can quickly run again the limited number of shared blocks allowed by the Linux kernel (4096, including all running processes)
- The choice of this number should heavily be tied into your choice of `-s` (total time stored, see below), and has been tuned at I-LOFAR to try target either 64 or 256 blocks in the final ringbuffer


#### -s (float):
- The amount of time to store in the final ringbuffer
- Your choice here heavily depends on the resources available to you, but we recommend keeping it above at least 5 seconds to allow for some context switching or hiccups from the consuming process


#### -r (int):
- The number of readers allowed to consume the ringbuffer
- This depends on your observing setup. In most cases, 1is recommended for a single process consuming the ringubffer, buta value of 2 might be needed if there is both an online processor and a ringbuffer-to-disk dump operator or transient dumper running.
- This value must exactly match the number of readers, if it is too high the recorder will hang after the ringbuffer is filled for the first time, if it is too low a consumer will be prevented from attaching to the ringbuffer.


#### -l (int):
- The number of batches of packets reads to perform before displaying observation statistics
- We print out information on the packet loss and observation progress periodically, this option control how often it is printed.


#### -z (float):
- Set the timeout on the UDP socket
- If no packets are received for this amount of time, the networking calls will hang-up and attempt to read data again


#### -e (int, not recommended,but can use 7824):
- Immediately set-up the ringbuffers on started for a given packet size
- This is not recommended incase of a configuration change on your station, but if you want to record every packet after the start of a beam this can be used to pre-allocate the ringbuffer and start recording immediately after packets start to be received from the station.


#### -f:
- If set, this option will kill any existing ringbuffers on the given input IDs and re-allocate new ones rather than exiting



#### -C:
- Ignore any sanity checks on the input times.

