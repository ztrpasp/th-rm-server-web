from scapy.all import Ether, ARP, sendp, get_if_hwaddr, IP, send
from scapy.layers.inet import TCP, UDP
from scapy.packet import Raw
from scapy.volatile import RandString
from CoLoRProtocol.CoLoRpacket import ColorGet, ColorData
import time


def toBytes(s):
    return bytes.fromhex(s)
if __name__ == "__main__":
    
    PIDs1 = [toBytes("abcd6655")]
    PIDs2 = [toBytes("dcba4433")]
    nid_agent = toBytes("b0cd69ef142db5a471676ad710eebf3a")  # 客户端NID
    nid_server = toBytes("d23454d19f307d8b98ff2da277c0b546")  # 服务器NID
    dpdk_mac = "00:0C:29:44:AE:5B"
    iface = "ens192"
    local_mac = get_if_hwaddr(iface)

    target_ip = "192.168.1.10"
    sender_ip = "192.168.1.20"

    cd = ColorData()
    cd = ColorData()
    cd.N_sid = nid_agent
    cd.L_sid = toBytes("54a6a073cc1fc870ef0f58ba7068a09d1c3ea68c")  # 随意一个文件的哈希值
    cd.Flags.F = False
    # F is 0表示报文来自域内，F is 1表示报文来自域外
    cd.Flags.S = False
    # S is 1时表示报文携带分片字段
    cd.PID_pt = 1
    # PID 指针，指向入域时需要边界路由器进行校验的 PID
    cd.nid_cus = nid_agent
    cd.PIDs = PIDs1

    pac = (
        Ether(src=local_mac, dst=dpdk_mac) /
        IP(
            src=sender_ip,
            dst=target_ip,
            ttl=64
        ) /
        cd
    )

    print(f"Sending ARP Reply on {iface}: {sender_ip} is-at {local_mac} → {dpdk_mac}")
    sendp(pac, iface=iface, verbose=True)