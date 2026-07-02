from color_packet import *

def as1_config():
    rm1_nid = 0x11111111111111111111111111111111.to_bytes(16, byteorder="big", signed=False)
    br11_nid = 0x11111111111111112222222222222222.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.0.66", dst="10.0.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
        nid=rm1_nid, as_id=1, sl=2, sk=0x12345678, 
        br_desc=[
            BRDescription(br_id=1, nid=br11_nid, ip_addr="10.0.0.2")
        ],
        as_desc=[
            ASDescription(as_id=2, sl=7),
        ], 
        px_desc=[
            InterdomainPathDescription(as_id=2, br_id=1, prefix_len=16, prefix=0x11220000),
        ])
    return pkt

def as2_config():
    rm2_nid = 0x22222222222222221111111111111111.to_bytes(16, byteorder="big", signed=False)
    br21_nid = 0x22222222222222222222222222222222.to_bytes(16, byteorder="big", signed=False)
    br22_nid = 0x22222222222222223333333333333333.to_bytes(16, byteorder="big", signed=False)
    pkt = IP(src="10.0.0.66", dst="10.0.0.1", proto=TrafficGen.COLOR_PROTO) / CONTROL() / ConfigRM(
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


as_config = [as1_config, as2_config, as3_config, as4_config]
as_id = int(input("AS ID: "))
assert 1 <= as_id <= 4, "AS ID can only be 1, 2, 3 or 4"
pkt = as_config[as_id - 1]()
wrpcap("t.pcap", [pkt])