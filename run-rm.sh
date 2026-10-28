#!/bin/bash
RM_LOG_STDERR=0 ./build/src/app/rm -l 1,2 -n 4 # RM使用1号核，只是用0号dpdk端口；不向终端打印JSON日志（仍写入rm.log）
# ./build/src/app/app -l 1 -n 4 --vdev=net_af_packet0,iface=ens160 # 虚拟PMD，仅限网卡不被dpdk支持的情况下使用，这样会损失80%-90%性能
