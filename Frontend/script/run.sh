echo 1 | tee /sys/class/net/enp197s0f1/device/sriov_numvfs
echo 1 | tee /sys/class/net/enp173s0f0/device/sriov_numvfs

ip netns add frontend_1
ip netns add frontend_2
ip netns add backend_1
ip netns add backend_2
ip netns add client

ip link set enp37s0f0 netns client
ip link set enp197s0f1 netns frontend_1
ip link set enp197s0f1v0  netns frontend_1
ip link set enp173s0f0 netns frontend_2
ip link set enp173s0f0v0  netns frontend_2
ip link set enp89s0f0 netns backend_1
ip link set enp89s0f1 netns backend_2

ip netns exec frontend_1 ifconfig enp197s0f1 up
ip netns exec frontend_2 ifconfig enp173s0f0 up
ip netns exec frontend_1 ifconfig enp197s0f1v0 up
ip netns exec frontend_2 ifconfig enp173s0f0v0 up
ip netns exec backend_1 ifconfig enp89s0f0 up
ip netns exec backend_2 ifconfig enp89s0f1 up
ip netns exec client ifconfig enp37s0f0 up

ip netns exec client ifconfig enp37s0f0 10.0.1.0/24
ip netns exec backend_1 ifconfig enp89s0f0 10.0.1.2/24
ip netns exec backend_2 ifconfig enp89s0f1 10.0.1.3/24
ip netns exec frontend_1 ifconfig enp197s0f1v0 10.0.1.252/24
ip netns exec frontend_2 ifconfig enp173s0f0v0 10.0.1.253/24

ip netns exec backend_1 arp -s 10.0.1.254 00:02:00:00:03:00 -i enp89s0f0
ip netns exec backend_2 arp -s 10.0.1.254 00:02:00:00:03:00 -i enp89s0f1
ip netns exec frontend_1 arp -s 10.0.1.254 00:02:00:00:03:00 -i enp197s0f1v0
ip netns exec frontend_2 arp -s 10.0.1.254 00:02:00:00:03:00 -i enp173s0f0v0
ip netns exec client arp -s 10.0.1.254 00:02:00:00:03:00 -i enp37s0f0
ip netns exec client arp -s 10.0.1.10 00:02:00:00:03:00 -i enp37s0f0

# ip netns exec frontend_1 ip route add default via 10.0.1.254
# ip netns exec frontend_2 ip route add default via 10.0.1.254
# ip netns exec backend_1 ip route add default via 10.0.1.254
# ip netns exec backend_2 ip route add default via 10.0.1.254
# ip netns exec client ip route add default via 10.0.1.254
# sudo mount -t bpf bpf /sys/fs/bpf
# ./L7LB_Server --file-prefix=frontend1

# sudo ip neigh del 10.0.1.254 dev enp197s0f1

# tofino
sudo ip neigh replace 10.0.1.252 lladdr 02:8b:4e:f6:6b:4a dev enp4s0 nud permanent
sudo ip neigh replace 10.0.1.253 lladdr 4a:d8:44:30:e4:2c dev enp4s0 nud permanent
sudo ip neigh add 10.0.1.2 lladdr b4:96:91:db:d6:78 dev enp4s0 nud permanent
sudo ip neigh add 10.0.1.3 lladdr b4:96:91:db:d6:79 dev enp4s0 nud permanent
sudo ip neigh add 10.0.1.0 lladdr b4:96:91:b1:d7:38 dev enp4s0 nud permanent

# frontend1
# sudo ip neigh add 10.0.1.2 lladdr b4:96:91:db:d6:78 dev enp197s0f1 nud permanent
# sudo ip neigh add 10.0.1.3 lladdr b4:96:91:db:d6:79 dev enp197s0f1 nud permanent
sudo ip neigh replace 10.0.1.253 lladdr 4a:d8:44:30:e4:2c dev enp197s0f1v0 nud permanent


# frontend2
# sudo ip neigh add 10.0.1.2 lladdr b4:96:91:db:d6:78 dev enp173s0f0 nud permanent
# sudo ip neigh add 10.0.1.3 lladdr b4:96:91:db:d6:79 dev enp173s0f0 nud permanent
sudo ip neigh replace 10.0.1.252 lladdr 02:8b:4e:f6:6b:4a dev enp173s0f0v0 nud permanent

# backend1
# sudo ip neigh add 10.0.1.252 lladdr 02:8b:4e:f6:6b:4a dev enp89s0f0 nud permanent
# sudo ip neigh add 10.0.1.253 lladdr 4a:d8:44:30:e4:2c dev enp89s0f0 nud permanent
# sudo ip neigh add 10.0.1.0 lladdr b4:96:91:b1:d7:38 dev enp89s0f0 nud permanent


# backend2
# sudo ip neigh add 10.0.1.252 lladdr 02:8b:4e:f6:6b:4a dev enp89s0f1 nud permanent
# sudo ip neigh add 10.0.1.253 lladdr 4a:d8:44:30:e4:2c dev enp89s0f1 nud permanent
# sudo ip neigh add 10.0.1.0 lladdr b4:96:91:b1:d7:38 dev enp89s0f1 nud permanent



# ip netns exec backend_1 nginx -c /etc/nginx/sites-enabled/backend1.conf
# ip netns exec backend_2 nginx -c /etc/nginx/sites-enabled/backend2.conf

# sudo ip netns exec backend_1 netstat -tulnp | grep :80
# sudo ip netns exec backend_2 netstat -tulnp | grep :80

# ./xdp_loader enp89s0f0
# ./xdp_loader enp89s0f1