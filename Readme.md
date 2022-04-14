ILTDada
=======

ILTDada is a UDP packet recorder for international LOFAR stations that transfers data to an intermediate [PSRDADA ringbuffer](http://psrdada.sourceforge.net/)
to allow for online processing of data.


Requirements
------------

### Software Requirements

This software depends on a modern version of CMake to gather and build the software and it's dependencies. However, some downstream dependencies need to be
installed prior to building the software

- CMake (> 3.14)
- [UDPPacketManager](https://github.com/David-McKenna/udpPacketManager) (> 0.7, built with CMake)
	- PSRDADA dependencies
		- autoconf, csh, libtool
	- libhdf5-dev, wget
	- (Strongly Recommended) Clang or Intel C compiler

On Debian-based systems, these can be installed with apt, while recent CMake versions can be installed with Python's pip package manager.

```shell
sudo apt install autoconf csh git libhdf5-dev libtool wget
sudo python3 -m pip install --upgrade cmake

# Optional
apt-get install clang libomp-dev libomp5
```

### Linux Kernel Requirements

The UDP packet receiver uses the [`recvmmsg`](https://man7.org/linux/man-pages/man2/recvmmsg.2.html) Linux kernel call to lower the overhead of capturing
packets. This however has the requirement that the number of packets being received can fit in the size of the UDP port buffer. By default, the maximum allowed
value is only between 100kb and 250kb (~40 - ~120 CEP packets after overheads) on most Linux kernel versions and needs to be increased as a result.

This limit can be increased by modifying the kernel parameters after every reboot using either of the following commands (to expand the maximum buffer to 1GB (
500MB with overheads, we will never allocate this much, though we recommend this value to be kept above 100MB),

```shell
sudo sysctl -w net.core.rmem_max=1073741824
sudo bash -c "echo '1073741824' > /proc/sys/net/core/rmem_max"
```

To make the fix permanent, you will need to modify your `sysctl.conf` file, which can be achieved with the following commands to modify the file, then reload
the kernel properties.

```shell
sudo bash -c "echo 'net.core.rmem_max=1073741824' >> /etc/sysctl.conf"
sudo sysctl -p
```

In some cases, you may also need to expand the maximum number of buffers, and the amount of memory that can be shared between processed (i.e., memory allocated
for the ringbuffer). This can be achieved in a similar manner by modifying the `SHMMNI`, `SHMMAX` and `SHMALL` kernel variables. It is recommended you keep the
total number of buffers below 4096, and the memory values below half of the total memory that a single CPU can allocate in your machine (i.e., if you have a
dual CPU server with 128GB of RAM, it is not recommended to increase the value above 32GB, though you can if you want to have a longer ringbuffer history
available).

You can check your current limits by using the command `ipcs -m -l`.

```shell
sudo bash -c "echo '4096' > /proc/sys/kernel/shmmni"
sudo bash -c "echo '1099511627776' > /proc/sys/kernel/shmmax"
sudo bash -c "echo '1099511627776' > /proc/sys/kernel/shmall"
```

or more permanently,

```shell
sudo bash -c "echo 'kernel.shmmni=4096' >> /etc/sysctl.conf"
sudo bash -c "echo 'kernel.shmmax=1099511627776' >> /etc/sysctl.conf"
sudo bash -c "echo 'kernel.shmall=1099511627776' >> /etc/sysctl.conf"
sudo sysctl -p
```

Build and Install
-----
The CMake build system handles most of the complexity of building the software. Once the pre-requists are met, you can build and install the software using the following command block (also found in **build.sh**).

```shell
mkdir build
cd build
cmake ..
cmake --build .
sudo cmake --install .
```

Known Issues
------------
 - PSRDADA end-of-data marker does not appear to be propagated to readers, requires timeout code in UPM to workaround

Usage
-----
See **[README_CLI.md](docs/README_CLI.md)** for usage documentation.

Funding
-------
This project was written while the author was receiving funding from the Irish Research Council's Government of Ireland Postgraduate Scholarship Program (
GOIPG/2019/2798).


Old notes
---------
```shell

Docker: sudo sysctl -w net.ipv6.conf.all.forwarding=1

/proc/sys/net/core/rmem_max caps socket memory

sudo sysctl -w kernel.shmmax=136365211648 sudo sysctl -w kernel.shmall=136365211648

docker run --rm -it -e TERM -v /mnt:/mnt --gpus all --ipc=host -p 16130:16130/udp -p 16131:16131/udp -p 16132:16132/udp -p 16133:16133/udp pulsar-gpu-dsp2021
```