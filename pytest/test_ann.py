from scapy.all import Ether, ARP, sendp, get_if_hwaddr, IP, send
from scapy.layers.inet import TCP, UDP
from scapy.packet import Raw
from scapy.volatile import RandString
from CoLoRProtocol.CoLoRpacket import *
import time

# DPDK 网卡的 MAC
dpdk_mac = "00:0C:29:29:70:0F"
# 你要用来发包的普通网卡接口
iface = "ens192"
# 本机在这张网卡上的 MAC
local_mac = get_if_hwaddr(iface)

def toBytes(s):
    return bytes.fromhex(s)

if __name__ == "__main__":
    et = Ether(src=local_mac, dst=dpdk_mac)
    
    pkt = IP(src="10.0.0.9",dst="10.0.0.1")
    su = Strategy_unit(tag=1, length=1, value=b"\x02")
    au = Ann_unit(AM=1, Strategy_unit_list=[su, su])
    au.N_sid = b'\x01'*16
    au.L_sid = b'\x02'*20
    au2 = Ann_unit(AM=1, Strategy_unit_list=[su, su])
    au2.N_sid = b'\x03'*16
    au2.L_sid = b'\x04'*20
    ca = ColorAnn(Announce_unit_list=[au, au2])
    sendp(et/pkt/ca,iface=iface,verbose=True)
    print("[+]Ann Packet sent")
    