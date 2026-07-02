#!/usr/bin/env python3
from scapy.all import Ether, ARP, sendp, get_if_hwaddr

# DPDK 网卡的 MAC
dpdk_mac = "00:0C:29:29:70:0F"
# 你要用来发包的普通网卡接口
iface = "ens192"
# 本机在这张网卡上的 MAC
local_mac = get_if_hwaddr(iface)

# 填成你想询问的目标IP和你自己的IP
target_ip = "10.0.0.1"  # 你想询问的IP
sender_ip = "10.0.0.128"   # 你自己的IP

arp = (
    Ether(src=local_mac, dst="ff:ff:ff:ff:ff:ff") /  # ARP请求使用广播地址
    ARP(
        op=1,              # 1 = who-has (ARP Request)
        hwsrc=local_mac,   # 发送者的MAC
        psrc=sender_ip,    # 发送者的IP
        hwdst="00:00:00:00:00:00",  # 目标MAC未知(全0)
        pdst=target_ip     # 你想询问的IP
    )
)

print(f"Sending ARP Request on {iface}: Who has {target_ip}? Tell {sender_ip}")
sendp(arp, iface=iface, verbose=True)