#!/usr/bin/env python3
"""
RM 服务器 GET 包测试脚本 (使用 Scapy 构造完整数据包)
可以构造并发送完整的 Ethernet + IPv4 + UDP + CoLoR GET 数据包

使用方法:
    python test_get_with_scapy.py --iface eth0 --dst-mac 00:11:22:33:44:55
"""

import argparse
import struct
import sys

try:
    from scapy.all import Ether, IP, UDP, Raw, sendp
except ImportError:
    print("需要安装 scapy: pip install scapy")
    sys.exit(1)

# GET 包头部结构大小
GET_HEADER_SIZE = 72


class GETPacket:
    """CoLoR GET 数据包构造器"""

    def __init__(self):
        self.version_package = 0x72  # CoLoR version + GET type
        self.ttl = 128
        self.pkg_len = GET_HEADER_SIZE
        self.checksum = 0
        self.MTU = 0
        self.PID_num = 0
        self.flag = 0
        self.Minimal_PID_CP = 0
        self.N_sid = bytearray(16)
        self.L_sid = bytearray(20)
        self.nid = bytearray(16)
        self.pids = []

    def set_sid(self, n_sid_hex, l_sid_hex):
        """设置 SID (十六进制字符串)"""
        self.N_sid = bytearray.fromhex(n_sid_hex)
        self.L_sid = bytearray.fromhex(l_sid_hex)

    def set_nid(self, nid_hex):
        """设置节点 ID (十六进制字符串)"""
        self.nid = bytearray.fromhex(nid_hex)

    def add_pid(self, pid):
        """添加路径标识符"""
        self.pids.append(pid)
        self.PID_num += 1
        self.pkg_len += 4

    def calculate_checksum(self, data):
        """计算 CoLoR 校验和"""
        checksum = 0
        for i in range(0, len(data), 2):
            if i + 1 < len(data):
                word = (data[i] << 8) | data[i + 1]
                checksum += word
            else:
                checksum += data[i] << 8

        while checksum >> 16:
            checksum = (checksum & 0xFFFF) + (checksum >> 16)

        return ~checksum & 0xFFFF

    def build(self, bad_checksum=False):
        """构建 GET 数据包"""
        data = bytearray()
        data.append(self.version_package)
        data.append(self.ttl)
        data.extend(struct.pack('!H', self.pkg_len))
        data.extend(struct.pack('!H', 0))  # 校验和占位
        data.extend(struct.pack('!H', self.MTU))
        data.append(self.PID_num)
        data.append(self.flag)
        data.extend(struct.pack('!H', self.Minimal_PID_CP))
        data.extend(self.N_sid)
        data.extend(self.L_sid)
        data.extend(self.nid)

        # 添加 PID
        for pid in self.pids:
            data.extend(struct.pack('!I', pid))

        # 计算校验和
        if bad_checksum:
            data[4:6] = struct.pack('!H', 0xDEAD)
        else:
            checksum = self.calculate_checksum(data)
            data[4:6] = struct.pack('!H', checksum)

        return bytes(data)


def create_test_packet(src_mac, dst_mac, src_ip, dst_ip,
                       n_sid_hex, l_sid_hex, nid_hex,
                       pids=None, bad_checksum=False):
    """创建完整的测试数据包"""
    # 构建 GET 负载
    get_pkt = GETPacket()
    get_pkt.set_sid(n_sid_hex, l_sid_hex)
    get_pkt.set_nid(nid_hex)

    if pids:
        for pid in pids:
            get_pkt.add_pid(pid)

    get_payload = get_pkt.build(bad_checksum=bad_checksum)

    # 构建完整数据包
    ether = Ether(src=src_mac, dst=dst_mac, type=0x0800)  # IPv4
    ip = IP(src=src_ip, dst=dst_ip, proto=17)  # UDP
    udp = UDP(sport=12345, dport=54321)
    raw = Raw(load=get_payload)

    return ether / ip / udp / raw


def main():
    parser = argparse.ArgumentParser(description="RM GET 包测试工具")
    parser.add_argument("--iface", default="eth0", help="发送网卡接口")
    parser.add_argument("--dst-mac", required=True, help="目标 MAC 地址")
    parser.add_argument("--src-mac", default="00:11:22:33:44:55", help="源 MAC 地址")
    parser.add_argument("--src-ip", default="192.168.1.100", help="源 IP 地址")
    parser.add_argument("--dst-ip", default="192.168.1.1", help="目标 IP 地址")

    args = parser.parse_args()

    print("=" * 60)
    print("RM 服务器 GET 包测试 (Scapy 版本)")
    print("=" * 60)
    print(f"发送接口: {args.iface}")
    print(f"目标 MAC: {args.dst_mac}")
    print(f"目标 IP:  {args.dst_ip}")

    # 测试用例 1: 匹配成功 (假设此 SID 已注册)
    print("\n[测试 1] 发送匹配成功的 GET 包 (SID 已注册)")
    print("-" * 60)
    pkt1 = create_test_packet(
        args.src_mac, args.dst_mac, args.src_ip, args.dst_ip,
        n_sid_hex="112233445566778899aabbccddeeff00",
        l_sid_hex="00112233445566778899aabbccddeeff00112233",
        nid_hex="00000000000000000000000000000001",
        pids=[0x00010000, 0x00020000]
    )
    sendp(pkt1, iface=args.iface, verbose=1)
    print("预期: SID 匹配成功，数据包被转发")

    # 测试用例 2: 匹配失败 (SID 不存在)
    print("\n[测试 2] 发送匹配失败的 GET 包 (SID 不存在)")
    print("-" * 60)
    pkt2 = create_test_packet(
        args.src_mac, args.dst_mac, args.src_ip, args.dst_ip,
        n_sid_hex="ffffffffffffffffffffffffffffffff",
        l_sid_hex="ffffffffffffffffffffffffffffffffffffffff",
        nid_hex="ffffffffffffffffffffffffffffffff",
        pids=[0x00010000]
    )
    sendp(pkt2, iface=args.iface, verbose=1)
    print("预期: SID 不存在，数据包被过滤丢弃")

    # 测试用例 3: 校验和错误
    print("\n[测试 3] 发送校验和错误的 GET 包")
    print("-" * 60)
    pkt3 = create_test_packet(
        args.src_mac, args.dst_mac, args.src_ip, args.dst_ip,
        n_sid_hex="112233445566778899aabbccddeeff00",
        l_sid_hex="00112233445566778899aabbccddeeff00112233",
        nid_hex="00000000000000000000000000000001",
        pids=[0x00010000],
        bad_checksum=True
    )
    sendp(pkt3, iface=args.iface, verbose=1)
    print("预期: 校验和错误，数据包被丢弃")

    # 测试用例 4: SID 列表请求 (全 0 SID)
    print("\n[测试 4] 发送 SID 列表请求 (全 0 SID)")
    print("-" * 60)
    pkt4 = create_test_packet(
        args.src_mac, args.dst_mac, args.src_ip, args.dst_ip,
        n_sid_hex="00000000000000000000000000000000",
        l_sid_hex="0000000000000000000000000000000000000000",
        nid_hex="00000000000000000000000000000001",
        pids=None
    )
    sendp(pkt4, iface=args.iface, verbose=1)
    print("预期: 返回 SID 列表")

    print("\n" + "=" * 60)
    print("所有测试包已发送!")
    print("=" * 60)
    print("\n请在 RM 服务器上观察:")
    print("  - 终端输出的处理信息")
    print("  - GET_stat.filtered_count (统计过滤的包)")
    print("  - GET_stat.to_inside_count (统计本域转发)")
    print("  - GET_stat.to_outside_count (统计跨域转发)")


if __name__ == "__main__":
    main()
