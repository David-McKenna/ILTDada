Docker: sudo sysctl -w net.ipv6.conf.all.forwarding=1

/proc/sys/net/core/rmem_max caps socket memory

sudo sysctl -w kernel.shmmax=136365211648
sudo sysctl -w kernel.shmall=136365211648

docker run --rm -it -e TERM -v /mnt:/mnt --gpus all --ipc=host -p 16130:16130/udp -p 16131:16131/udp -p 16132:16132/udp -p 16133:16133/udp pulsar-gpu-dsp2021