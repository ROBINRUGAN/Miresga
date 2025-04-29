ip netns exec frontend_1 ip link set enp197s0f1 netns 1
ip netns exec frontend_2 ip link set enp173s0f0 netns 1
ip netns exec backend_1 ip link set enp89s0f0 netns 1
ip netns exec backend_2 ip link set enp89s0f1 netns 1
ip netns exec client ip link set enp37s0f0 netns 1
echo 0 | tee /sys/class/net/enp197s0f1/device/sriov_numvfs
echo 0 | tee /sys/class/net/enp173s0f0/device/sriov_numvfs
ip netns del frontend_1
ip netns del frontend_2
ip netns del backend_1
ip netns del backend_2
ip netns del client