#!/usr/bin/env python3
from scapy.all import Ether, ARP, sendp, get_if_hwaddr

# DPDK 网卡的 MAC
dpdk_mac = "00:0C:29:44:AE:5B"
# 你要用来发包的普通网卡接口
iface = "ens192"
# 本机在这张网卡上的 MAC
local_mac = get_if_hwaddr(iface)

# 填成你想模拟的“被回复的”目标 IP 和回复者 IP
# 比如假设 DPDK 程序的 IP 是 192.168.1.10，你想告诉它“192.168.1.10 对应 MAC local_mac”
target_ip = "192.168.1.10"
sender_ip = "192.168.1.20"

arp = (
    Ether(src=local_mac, dst=dpdk_mac) /
    ARP(
        op=2,              # 2 = is-at (ARP Reply)
        hwsrc=local_mac,   # 回复者的 MAC
        psrc=sender_ip,    # 回复者想要映射的 IP
        hwdst=dpdk_mac,    # 接收者（DPDK）的 MAC
        pdst=target_ip     # 接收者（DPDK）认为要询问的 IP
    )
)

print(f"Sending ARP Reply on {iface}: {sender_ip} is-at {local_mac} → {dpdk_mac}")
sendp(arp, iface=iface, verbose=True)
