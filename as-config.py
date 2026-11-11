#!/home/zeusnet/python3.6/bin/python3.6

# 这个文件是用来根据拓扑信息生成RM邻域信息配置文件config_recieve_file的
# 记得生成完文件，把config_recieve_file拷贝到~/下

from color_packet.color_packet import *
import scapy
from scapy import *
import json

def write_control_config_to_file(pkt, filename="config_recieve_file"):
    """
    提取 pkt 中从 CONTROL() 层开始直到尾部的所有字节，
    并写入二进制文件 filename。
    """
    # 确保 CONTROL 层存在
    if CONTROL not in pkt:
        raise ValueError("Packet 中未发现 CONTROL 层")
    # raw() 会返回从 CONTROL 层开始到尾部的原始字节
    payload_bytes = raw(pkt[CONTROL])
    # 将这些字节写入文件
    with open(filename, "wb") as f:
        f.write(payload_bytes)
    print(f"已将 CONTROL()/ConfigRM() 部分写入文件 {filename}，共 {len(payload_bytes)} 字节。")

def as1_config():
    rm1_nid = 0x11111111111111111111111111111111.to_bytes(16, byteorder="big", signed=False)
    br11_nid = 0x11111111111111112222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.10.0.5", dst="10.10.0.2", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm1_nid, as_id=1, sl=2, sk=0x12345678, 
        br_desc=[
            BRDescription(br_id=1, nid=br11_nid, ip_addr="10.0.0.2")
        ],
        as_desc=[
            ASDescription(as_id=2, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x11220000),
        ])
    return pkt

def as2_config():
    rm2_nid = 0x22222222222222221111111111111111.to_bytes(16, byteorder="big", signed=False)
    br21_nid = 0x22222222222222222222222222222222.to_bytes(16, byteorder="big", signed=False)
    br22_nid = 0x22222222222222223333333333333333.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="172.10.0.5", dst="172.10.0.2", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm2_nid, as_id=2, sl=7, sk=0x87654321, 
        br_desc=[
            BRDescription(br_id=1, nid=br21_nid, ip_addr="10.0.0.2"),
            BRDescription(br_id=2, nid=br22_nid, ip_addr="10.0.0.3"),
        ], 
        as_desc=[
            ASDescription(as_id=1, sl=2),
            ASDescription(as_id=3, sl=7),
            ASDescription(as_id=4, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x11220000),
            InterdomainPathDescription(as_id=3, br_id=2, prefix_len=16, prefix=0x33440000),
            InterdomainPathDescription(as_id=4, br_id=1, prefix_len=16, prefix=0x55660000),
        ])
    return pkt

def as3_config():
    rm3_nid = 0x33333333333333331111111111111111.to_bytes(16, byteorder="big", signed=False)
    br31_nid = 0x33333333333333332222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.1.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm3_nid, as_id=3, sl=7, sk=0x12341234, 
        br_desc=[
            BRDescription(br_id=1, nid=br31_nid, ip_addr="10.0.1.2"),
        ], 
        as_desc=[
            ASDescription(as_id=2, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x33440000),
        ])
    return pkt

def as4_config():
    rm4_nid = 0x44444444444444441111111111111111.to_bytes(16, byteorder="big", signed=False)
    br41_nid = 0x44444444444444442222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.1.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm4_nid, as_id=4, sl=7, sk=0x56785678, 
        br_desc=[
            BRDescription(br_id=1, nid=br41_nid, ip_addr="10.0.1.2"),
        ],
        as_desc=[
            ASDescription(as_id=2, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x55660000),
        ])
    return pkt

# # 专网1(原AS2)
# def the_new_color_as1_config():
#     rm1_nid = 0x11111111111111111111111111111111.to_bytes(16, byteorder="big", signed=False)
#     br11_nid = 0x11111111111111112222222222222222.to_bytes(16, byteorder="big", signed=False)
#     br12_nid = 0x11111111111111113333333333333333.to_bytes(16, byteorder="big", signed=False)
#     pkt = IP(src="10.0.0.66", dst="10.0.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
#         nid=rm1_nid, as_id=1, sl=7, sk=0x87654321, 
#         br_desc=[
#             BRDescription(br_id=1, nid=br11_nid, ip_addr="10.0.0.2"),
#             BRDescription(br_id=2, nid=br12_nid, ip_addr="10.0.0.3")
#         ],
#         as_desc=[
#             ASDescription(as_id=2, sl=7),
#             ASDescription(as_id=3, sl=7),
#             ASDescription(as_id=15, sl=2),
#         ], 
#         px_desc=[
#             InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x55660000),
#             InterdomainPathDescription(as_id=3, br_id=2, prefix_len=16, prefix=0x33440000),
#             InterdomainPathDescription(as_id=15, br_id=1, prefix_len=16, prefix=0x11220000),
#         ])
#     return pkt

# # 专网2(原AS4)
# def the_new_color_as2_config():
#     rm2_nid = 0x22222222222222221111111111111111.to_bytes(16, byteorder="big", signed=False)
#     br21_nid = 0x22222222222222222222222222222222.to_bytes(16, byteorder="big", signed=False)
#     pkt = IP(src="10.0.0.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
#         nid=rm2_nid, as_id=2, sl=7, sk=0x56785678, 
#         br_desc=[
#             BRDescription(br_id=1, nid=br21_nid, ip_addr="10.0.1.2"),
#         ],
#         as_desc=[
#             ASDescription(as_id=1, sl=7),
#         ], 
#         px_desc=[
#             InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x55660000),
#         ])
#     return pkt

# # 专网3(原AS3)
# def the_new_color_as3_config():
#     rm3_nid = 0x33333333333333331111111111111111.to_bytes(16, byteorder="big", signed=False)
#     br31_nid = 0x33333333333333332222222222222222.to_bytes(16, byteorder="big", signed=False)
#     pkt = IP(src="10.0.0.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
#         nid=rm3_nid, as_id=3, sl=7, sk=0x12341234, 
#         br_desc=[
#             BRDescription(br_id=1, nid=br31_nid, ip_addr="10.0.1.2"),
#         ],
#         as_desc=[
#             ASDescription(as_id=1, sl=7),
#         ], 
#         px_desc=[
#             InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x33440000),
#         ])
#     return pkt

# # 公网5(原AS1，现AS15)
# def the_new_color_as15_config():
#     rm15_nid = 0x15151515151515151111111111111111.to_bytes(16, byteorder="big", signed=False)
#     br151_nid = 0x15151515151515152222222222222222.to_bytes(16, byteorder="big", signed=False)
#     pkt = IP(src="10.134.0.66", dst="10.134.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
#         nid=rm15_nid, as_id=15, sl=2, sk=0x12345678, 
#         br_desc=[
#             BRDescription(br_id=1, nid=br151_nid, ip_addr="10.134.0.2"),
#         ],
#         as_desc=[
#             ASDescription(as_id=1, sl=7),
#         ], 
#         px_desc=[
#             InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x11220000),
#         ])
#     return pkt

# 目前这段代码有问题，别用
'''
您可以将RM需要的邻域AS信息写入neighbor.json，之后用这个函数和write_control_config_to_file，
就可以快速生成RM需要的初始化文件config_recieve_file
或者执行该函数生成管理员通告报文pkt(注意如果这么做您需要修改pkt的ip地址，函数里有)
然后把网线接到RM所在交换机上，执行send(pkt)把报文发给RM，也可以起到初始化的目的。
'''
def write_as_config_with_json():
    try:
        with open('neighbor.json') as f:
            data = json.load(f)
    except FileNotFoundError:
        print("文件未找到")
    except json.JSONDecodeError:
        print("JSON格式错误")
    rm_nid = int(data["rm_nid"],16).to_bytes(16, byteorder="big", signed=False)
    br_num = len(data["br_nid"])
    br_nid = []
    for item in data["br_nid"]:
        br_nid.append(int(item,16).to_bytes(16, byteorder="big", signed=False))

    br_desc = []
    for item in data["br_desc"]:
        br_desc.append(BRDescription(br_id=item["br_id"],nid=int(item["nid"],16).to_bytes(16, byteorder="big", signed=False),ip_addr=item["ip_addr"]))

    as_desc = []
    for item in data["as_desc"]:
        as_desc.append(ASDescription(as_id=item["as_id"],sl=item["sl"]))

    px_desc = []
    for item in data["px_desc"]:
        px_desc.append(InterdomainPathDescription(as_id=item["as_id"],
                                                  br_id=item["br_id"],
                                                  prefix_len=item["prefix_len"],
                                                  prefix=int(item["prefix"],16)))

    pkt = IP(src="10.134.0.66", dst="10.134.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm_nid, as_id=data["as_id"], sl=data["sl"],sk=int(data["sk"],16),
        br_desc=br_desc,
        as_desc=as_desc,
        px_desc=px_desc
    )

    return pkt


################################################################
# 专网1
def the_new_color_as1_config():
    print('as1')
    rm1_nid = 0x00000000000000122222222222222222.to_bytes(16, byteorder="big", signed=False)
    br1_nid = 0x00000000000000111111111111111111.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.10.0.5", dst="10.10.0.5", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm1_nid, as_id=1, sl=2, sk=0x12340001,
        br_desc=[
            BRDescription(br_id=1, nid=br1_nid, ip_addr="10.10.0.1")
        ],
        as_desc=[
            ASDescription(as_id=2, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x10000000),
        ])
    return pkt

# 专网2
def the_new_color_as2_config():
    rm2_nid = 0x00000000000000021111111111111111.to_bytes(16, byteorder="big", signed=False)
    br21_nid = 0x00000000000000022222222222222222.to_bytes(16, byteorder="big", signed=False)
    br24_nid = 0x00000000000000022222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="172.10.0.5", dst="172.10.0.2", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm2_nid, as_id=2, sl=7, sk=0x12340002, 
        br_desc=[
            BRDescription(br_id=1, nid=br21_nid, ip_addr="172.10.0.1"),
            BRDescription(br_id=2, nid=br24_nid, ip_addr="172.10.0.1"),
        ],
        as_desc=[
            ASDescription(as_id=1, sl=7),
            ASDescription(as_id=4, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x10000000),
            InterdomainPathDescription(as_id=4, br_id=2, prefix_len=16, prefix=0x24000000),
        ])
    return pkt

# 专网3
def the_new_color_as3_config():
    rm3_nid = 0x00000000000000031111111111111111.to_bytes(16, byteorder="big", signed=False)
    br31_nid = 0x00000000000000032222222222222222.to_bytes(16, byteorder="big", signed=False)
    br32_nid = 0x00000000000000033333333333333333.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.1.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm3_nid, as_id=3, sl=7, sk=0x12340003, 
        br_desc=[
            BRDescription(br_id=1, nid=br31_nid, ip_addr="10.0.3.2"),
            BRDescription(br_id=2, nid=br32_nid, ip_addr="10.0.1.2"),
        ],
        as_desc=[
            ASDescription(as_id=1, sl=7),
            ASDescription(as_id=10, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0x000d0000),
            InterdomainPathDescription(as_id=10, br_id=2, prefix_len=16, prefix=0xff030000),
        ])
    return pkt

# 公网0
def the_new_color_as10_config():
    rm10_nid = 0x00000000000000101111111111111111.to_bytes(16, byteorder="big", signed=False)
    br101_nid = 0x00000000000000102222222222222222.to_bytes(16, byteorder="big", signed=False)
    br102_nid = 0x00000000000000103333333333333333.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.0.66", dst="10.134.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm10_nid, as_id=10, sl=2, sk=0x12340010, 
        br_desc=[
            BRDescription(br_id=1, nid=br101_nid, ip_addr="10.134.0.2"),
            BRDescription(br_id=2, nid=br102_nid, ip_addr="10.134.0.3"),
        ],
        as_desc=[
            ASDescription(as_id=3, sl=7),
            ASDescription(as_id=4, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=3, br_id=1, prefix_len=16, prefix=0xff030000),
            InterdomainPathDescription(as_id=4, br_id=2, prefix_len=16, prefix=0xff040000),
        ])
    return pkt


# 公网5
def the_new_color_as15_config():
    rm15_nid = 0x00000000000000151111111111111111.to_bytes(16, byteorder="big", signed=False)
    br151_nid = 0x00000000000000152222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.5.66", dst="10.134.5.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm15_nid, as_id=15, sl=2, sk=0x12340015, 
        br_desc=[
            BRDescription(br_id=1, nid=br151_nid, ip_addr="10.134.5.2"),
        ],
        as_desc=[
            ASDescription(as_id=1, sl=7),
            ASDescription(as_id=0, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=1, br_id=1, prefix_len=16, prefix=0xff0f0000),
            InterdomainPathDescription(as_id=0, br_id=1, prefix_len=16, prefix=0xff050000),
        ])
    return pkt

# 公网2
def the_new_color_as12_config():
    rm12_nid = 0x00000000000000121111111111111111.to_bytes(16, byteorder="big", signed=False)
    br121_nid = 0x00000000000000122222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.2.66", dst="10.134.2.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm12_nid, as_id=12, sl=2, sk=0x12340012, 
        br_desc=[
            BRDescription(br_id=1, nid=br121_nid, ip_addr="10.134.2.2"),
        ],
        as_desc=[
            ASDescription(as_id=3, sl=7),
            ASDescription(as_id=11, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=3, br_id=1, prefix_len=16, prefix=0xff170000),
            InterdomainPathDescription(as_id=11, br_id=1, prefix_len=16, prefix=0x000c0000),
        ])
    return pkt

# 公网1
def the_new_color_as11_config():
    rm11_nid = 0x00000000000000111111111111111111.to_bytes(16, byteorder="big", signed=False)
    br111_nid = 0x00000000000000112222222222222222.to_bytes(16, byteorder="big", signed=False)
    br112_nid = 0x00000000000000113333333333333333.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.1.66", dst="10.134.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm11_nid, as_id=11, sl=2, sk=0x12340011, 
        br_desc=[
            BRDescription(br_id=1, nid=br111_nid, ip_addr="10.134.1.2"),
            BRDescription(br_id=2, nid=br112_nid, ip_addr="10.134.1.3"),
        ],
        as_desc=[
            ASDescription(as_id=0, sl=2),
            ASDescription(as_id=12, sl=2),
            ASDescription(as_id=13, sl=2),
            ASDescription(as_id=4, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=0, br_id=1, prefix_len=16, prefix=0xff010000),
            InterdomainPathDescription(as_id=12, br_id=1, prefix_len=16, prefix=0x000c0000),
            InterdomainPathDescription(as_id=13, br_id=2, prefix_len=16, prefix=0x000d0000),
            InterdomainPathDescription(as_id=4, br_id=2, prefix_len=16, prefix=0xff0e0000),
        ])
    return pkt

# 公网3
def the_new_color_as13_config():
    rm13_nid = 0x00000000000000131111111111111111.to_bytes(16, byteorder="big", signed=False)
    br131_nid = 0x00000000000000132222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.3.66", dst="10.134.3.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm13_nid, as_id=13, sl=2, sk=0x12340013, 
        br_desc=[
            BRDescription(br_id=1, nid=br131_nid, ip_addr="10.134.3.2"),
        ],
        as_desc=[
            ASDescription(as_id=11, sl=2),
            ASDescription(as_id=14, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=11, br_id=1, prefix_len=16, prefix=0x000d0000),
            InterdomainPathDescription(as_id=14, br_id=1, prefix_len=16, prefix=0x00220000),
        ])
    return pkt

# 公网4
def the_new_color_as14_config():
    rm14_nid = 0x00000000000000141111111111111111.to_bytes(16, byteorder="big", signed=False)
    br141_nid = 0x00000000000000142222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.134.4.66", dst="10.134.4.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm14_nid, as_id=14, sl=2, sk=0x12340014, 
        br_desc=[
            BRDescription(br_id=1, nid=br141_nid, ip_addr="10.134.4.2"),
        ],
        as_desc=[
            ASDescription(as_id=13, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=13, br_id=1, prefix_len=16, prefix=0x00220000),
        ])
    return pkt

# 专网4
def the_new_color_as4_config():
    rm4_nid = 0x00000000000000041111111111111111.to_bytes(16, byteorder="big", signed=False)
    br41_nid = 0x00000000000000042222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.1.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm4_nid, as_id=4, sl=7, sk=0x12340004, 
        br_desc=[
            BRDescription(br_id=1, nid=br41_nid, ip_addr="10.0.1.2"),
        ],
        as_desc=[
            ASDescription(as_id=10, sl=2),
            ASDescription(as_id=0, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=10, br_id=1, prefix_len=16, prefix=0xff040000),
            InterdomainPathDescription(as_id=0, br_id=1, prefix_len=16, prefix=0x00040000),
        ])
    return pkt

# 专网5
def the_new_color_as5_config():
    rm5_nid = 0x00000000000000051111111111111111.to_bytes(16, byteorder="big", signed=False)
    br51_nid = 0x00000000000000052222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.1.66", dst="10.0.1.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm5_nid, as_id=5, sl=7, sk=0x12340005, 
        br_desc=[
            BRDescription(br_id=1, nid=br51_nid, ip_addr="10.0.1.2"),
        ],
        as_desc=[
            ASDescription(as_id=0, sl=2),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=0, br_id=1, prefix_len=16, prefix=0x00050000),
        ])
    return pkt

# 卫星网络0
def the_new_color_as0_config():
    rm0_nid = 0x00000000000000001111111111111111.to_bytes(16, byteorder="big", signed=False)
    br01_nid = 0x00000000000000002222222222222222.to_bytes(16, byteorder="big", signed=False)
    br02_nid = 0x00000000000000003333333333333333.to_bytes(16, byteorder="big", signed=False)
    br03_nid = 0x00000000000000004444444444444444.to_bytes(16, byteorder="big", signed=False)
    br04_nid = 0x00000000000000005555555555555555.to_bytes(16, byteorder="big", signed=False)
    br05_nid = 0x00000000000000006666666666666666.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.50.51.66", dst="10.50.51.0", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm0_nid, as_id=0, sl=2, sk=0x12340000, 
        br_desc=[
            BRDescription(br_id=1, nid=br01_nid, ip_addr="10.50.51.2"),
            BRDescription(br_id=2, nid=br02_nid, ip_addr="10.50.51.4"),
            BRDescription(br_id=3, nid=br03_nid, ip_addr="10.50.51.6"),
            BRDescription(br_id=4, nid=br04_nid, ip_addr="10.50.51.8"),
            BRDescription(br_id=5, nid=br05_nid, ip_addr="10.50.51.10"),
        ],
        as_desc=[
            ASDescription(as_id=15, sl=2),
            ASDescription(as_id=1, sl=7),
            ASDescription(as_id=11, sl=2),
            ASDescription(as_id=4, sl=7),
            ASDescription(as_id=5, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=15, br_id=1, prefix_len=16, prefix=0xff050000),
            InterdomainPathDescription(as_id=1, br_id=2, prefix_len=16, prefix=0x00010000),
            InterdomainPathDescription(as_id=11, br_id=3, prefix_len=16, prefix=0xff010000),
            InterdomainPathDescription(as_id=4, br_id=4, prefix_len=16, prefix=0x00040000),
            InterdomainPathDescription(as_id=5, br_id=5, prefix_len=16, prefix=0x00050000),
        ])
    return pkt

# as_config = [as1_config, as2_config, as3_config, as4_config]
# as_id = int(input("AS ID: "))
# assert 1 <= as_id <= 4, "AS ID can only be 1, 2, 3 or 4"
# pkt = as_config[as_id - 1]()
# write_control_config_to_file(pkt)
# send(pkt)

as_config = [the_new_color_as1_config, the_new_color_as2_config, the_new_color_as3_config, the_new_color_as15_config, 
             the_new_color_as12_config, the_new_color_as11_config, the_new_color_as13_config, the_new_color_as14_config,
             the_new_color_as4_config, the_new_color_as5_config, the_new_color_as0_config, the_new_color_as10_config]
asx = [1,2,3,15,12,11,13,14,4,5,0,10]
as_id = int(input("AS ID:"))
for i in range(len(asx)):
    if asx[i] == as_id:
        pkt = as_config[i]()
        write_control_config_to_file(pkt)
        break

# pkt = write_as_config_with_json()
# write_control_config_to_file(pkt)


