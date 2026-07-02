#!/usr/bin/env python3
"""
RM 服务器 GET 包测试脚本
测试 GET 包匹配成功和失败的情况

使用方法:
    python test_get_packet.py [服务器IP] [服务器端口]
"""

import socket
import struct
import sys
import time

# CoLoR GET 包头部大小 (72 bytes)
# GET_front_t 结构:
#   version_package (1) + ttl (1) + pkg_len (2) + checksum (2) + MTU (2) +
#   PID_num (1) + flag (1) + Minimal_PID_CP (2) +
#   N_sid (16) + L_sid (20) + nid (16) = 72 bytes
GET_HEADER_SIZE = 72

DEFAULT_RM_IP = "127.0.0.1"
DEFAULT_RM_PORT = 12345


class GETPacket:
    """CoLoR GET 数据包构造器"""

    def __init__(self):
        self.version_package = 0x72  # CoLoR 版本 + 包类型
        self.ttl = 128
        self.pkg_len = GET_HEADER_SIZE
        self.checksum = 0
        self.MTU = 0
        self.PID_num = 0
        self.flag = 0
        self.Minimal_PID_CP = 0
        self.N_sid = bytearray(16)  # 服务标识符
        self.L_sid = bytearray(20)  # 定位标识符
        self.nid = bytearray(16)    # 节点标识符
        self.pids = []  # PID 列表 (每个 4 bytes)

    def set_sid(self, n_sid_bytes, l_sid_bytes):
        """设置 N_sid 和 L_sid"""
        if len(n_sid_bytes) == 16:
            self.N_sid = bytearray(n_sid_bytes)
        if len(l_sid_bytes) == 20:
            self.L_sid = bytearray(l_sid_bytes)

    def set_nid(self, nid_bytes):
        """设置节点标识符"""
        if len(nid_bytes) == 16:
            self.nid = bytearray(nid_bytes)

    def add_pid(self, pid):
        """添加 PID (路径标识符)"""
        self.pids.append(pid)
        self.PID_num += 1
        self.pkg_len += 4

    def calculate_checksum(self, data):
        """计算校验和 (简化版)"""
        checksum = 0
        for i in range(0, len(data), 2):
            if i + 1 < len(data):
                word = (data[i] << 8) | data[i + 1]
                checksum += word
            else:
                checksum += data[i] << 8

        # 处理进位
        while checksum >> 16:
            checksum = (checksum & 0xFFFF) + (checksum >> 16)

        return ~checksum & 0xFFFF

    def build(self):
        """构建完整的 GET 数据包"""
        # 先构建基础数据包 (checksum = 0)
        data = bytearray()
        data.append(self.version_package)
        data.append(self.ttl)
        data.extend(struct.pack('!H', self.pkg_len))
        data.extend(struct.pack('!H', 0))  # checksum 占位
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

        # 计算并设置校验和
        checksum = self.calculate_checksum(data)
        data[4:6] = struct.pack('!H', checksum)

        return bytes(data)


def build_ipv4_header(src_ip, dst_ip, payload_len, proto=17):  # proto=17 is UDP
    """构建 IPv4 头部"""
    src_bytes = socket.inet_aton(src_ip)
    dst_bytes = socket.inet_aton(dst_ip)

    ip_header = bytearray()
    ip_header.append(0x45)  # version + IHL
    ip_header.append(0x00)  # TOS
    ip_header.extend(struct.pack('!H', 20 + payload_len))  # total len
    ip_header.extend(struct.pack('!H', 0x1234))  # identification
    ip_header.extend(struct.pack('!H', 0x0000))  # flags + frag offset
    ip_header.append(64)  # TTL
    ip_header.append(proto)  # protocol
    ip_header.extend(struct.pack('!H', 0x0000))  # checksum
    ip_header.extend(src_bytes)
    ip_header.extend(dst_bytes)

    return bytes(ip_header)


def build_udp_header(src_port, dst_port, payload_len):
    """构建 UDP 头部"""
    udp_header = bytearray()
    udp_header.extend(struct.pack('!H', src_port))
    udp_header.extend(struct.pack('!H', dst_port))
    udp_header.extend(struct.pack('!H', 8 + payload_len))  # length
    udp_header.extend(struct.pack('!H', 0x0000))  # checksum (optional)
    return bytes(udp_header)


def send_get_packet(rm_ip, rm_port, n_sid, l_sid, nid, pids=None, test_name=""):
    """发送 GET 数据包到 RM 服务器"""
    pkt = GETPacket()
    pkt.set_sid(n_sid, l_sid)
    pkt.set_nid(nid)

    if pids:
        for pid in pids:
            pkt.add_pid(pid)

    get_data = pkt.build()

    # 使用 UDP 发送
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(get_data, (rm_ip, rm_port))
        print(f"[{test_name}] 已发送 GET 包")
        print(f"  N_sid: {n_sid.hex()}")
        print(f"  L_sid: {l_sid.hex()}")
        print(f"  包大小: {len(get_data)} bytes")
        if pids:
            print(f"  PID 数量: {len(pids)}")
            for i, pid in enumerate(pids):
                print(f"    PID[{i}]: 0x{pid:08x}")
        return True
    except Exception as e:
        print(f"[{test_name}] 发送失败: {e}")
        return False
    finally:
        sock.close()


def test_match_success(rm_ip, rm_port):
    """测试场景 1: GET 包匹配成功 (SID 存在于 RM 表中)"""
    print("\n" + "=" * 60)
    print("测试场景 1: GET 包匹配成功 (SID 存在)")
    print("=" * 60)

    # 假设有一个已知存在的 SID (根据实际 RM 配置调整)
    # 这里使用一个示例 SID，实际测试时需要替换为 RM 中已注册的 SID
    n_sid = bytes.fromhex("112233445566778899aabbccddeeff00")  # 16 bytes
    l_sid = bytes.fromhex("00112233445566778899aabbccddeeff00112233")  # 20 bytes
    nid = bytes.fromhex("00000000000000000000000000000001")  # 16 bytes

    # 添加示例 PID (路径信息)
    pids = [0x00010000, 0x00020000]

    send_get_packet(rm_ip, rm_port, n_sid, l_sid, nid, pids,
                    "匹配成功测试")

    print("\n预期结果:")
    print("  - RM 服务器找到对应的 SID")
    print("  - 如果提供者在本域，向终端转发")
    print("  - 如果提供者在其他域，向 BR 转发")
    print("  - GET_stat.to_inside_count 或 to_outside_count 增加")


def test_match_failure_unknown_sid(rm_ip, rm_port):
    """测试场景 2: GET 包匹配失败 (SID 不存在)"""
    print("\n" + "=" * 60)
    print("测试场景 2: GET 包匹配失败 (SID 不存在)")
    print("=" * 60)

    # 使用一个不存在的 SID
    n_sid = bytes.fromhex("ffffffffffffffffffffffffffffffff")  # 16 bytes
    l_sid = bytes.fromhex("ffffffffffffffffffffffffffffffffffffffff")  # 20 bytes
    nid = bytes.fromhex("ffffffffffffffffffffffffffffffff")  # 16 bytes

    pids = [0x00010000]

    send_get_packet(rm_ip, rm_port, n_sid, l_sid, nid, pids,
                    "SID 不存在测试")

    print("\n预期结果:")
    print("  - RM 服务器在 SID 表中找不到匹配项")
    print("  - 数据包被丢弃")
    print("  - GET_stat.filtered_count 增加")


def test_match_failure_bad_checksum(rm_ip, rm_port):
    """测试场景 3: GET 包匹配失败 (校验和错误)"""
    print("\n" + "=" * 60)
    print("测试场景 3: GET 包匹配失败 (校验和错误)")
    print("=" * 60)

    # 构造一个校验和错误的包
    pkt = GETPacket()
    n_sid = bytes.fromhex("112233445566778899aabbccddeeff00")
    l_sid = bytes.fromhex("00112233445566778899aabbccddeeff00112233")
    nid = bytes.fromhex("00000000000000000000000000000001")

    pkt.set_sid(n_sid, l_sid)
    pkt.set_nid(nid)

    # 构建包但不设置正确的校验和 (设为 0)
    data = bytearray()
    data.append(pkt.version_package)
    data.append(pkt.ttl)
    data.extend(struct.pack('!H', pkt.pkg_len))
    data.extend(struct.pack('!H', 0xABCD))  # 错误的校验和
    data.extend(struct.pack('!H', pkt.MTU))
    data.append(pkt.PID_num)
    data.append(pkt.flag)
    data.extend(struct.pack('!H', pkt.Minimal_PID_CP))
    data.extend(pkt.N_sid)
    data.extend(pkt.L_sid)
    data.extend(pkt.nid)

    get_data = bytes(data)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(get_data, (rm_ip, rm_port))
        print(f"[校验和错误测试] 已发送 GET 包")
        print(f"  N_sid: {n_sid.hex()}")
        print(f"  错误的校验和: 0xABCD")
    except Exception as e:
        print(f"[校验和错误测试] 发送失败: {e}")
    finally:
        sock.close()

    print("\n预期结果:")
    print("  - RM 服务器检测到校验和错误")
    print("  - 数据包被丢弃")
    print("  - GET_stat.filtered_count 增加")


def test_match_sid_list_request(rm_ip, rm_port):
    """测试场景 4: SID 列表请求 (全 0 SID)"""
    print("\n" + "=" * 60)
    print("测试场景 4: SID 列表请求 (全 0 SID)")
    print("=" * 60)

    # 全 0 SID 表示请求 SID 列表
    n_sid = bytes(16)  # 全 0
    l_sid = bytes(20)  # 全 0
    nid = bytes.fromhex("00000000000000000000000000000001")

    send_get_packet(rm_ip, rm_port, n_sid, l_sid, nid, None,
                    "SID 列表请求")

    print("\n预期结果:")
    print("  - RM 服务器识别为 SID 列表请求")
    print("  - 返回 SID-Lists 给请求者")


def main():
    if len(sys.argv) >= 2:
        rm_ip = sys.argv[1]
    else:
        rm_ip = DEFAULT_RM_IP

    if len(sys.argv) >= 3:
        rm_port = int(sys.argv[2])
    else:
        rm_port = DEFAULT_RM_PORT

    print("RM 服务器 GET 包测试")
    print(f"目标服务器: {rm_ip}:{rm_port}")
    print("=" * 60)

    # 运行所有测试场景
    test_match_success(rm_ip, rm_port)
    time.sleep(0.5)

    test_match_failure_unknown_sid(rm_ip, rm_port)
    time.sleep(0.5)

    test_match_failure_bad_checksum(rm_ip, rm_port)
    time.sleep(0.5)

    test_match_sid_list_request(rm_ip, rm_port)

    print("\n" + "=" * 60)
    print("所有测试包已发送!")
    print("=" * 60)
    print("\n请在 RM 服务器上观察:")
    print("  1. 终端输出的处理日志")
    print("  2. GET_stat 统计计数的变化")
    print("  3. 数据包转发情况")


if __name__ == "__main__":
    main()
