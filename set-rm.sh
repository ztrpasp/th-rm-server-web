#!/bin/bash
# 请将这个文件放在 ~/目录下(例如/home/color/目录下)
# dpdk需要大页内存才能运行，这里的意思就是设置单页大小为2M，总计2G的大页内存，共计1024页
# dpdk一般只允许单页大小设为2M或1G，总大小是多多益善(建议2G以上)
python3 ./dpdk-hugepages.py --pagesize 2M --setup 2G
# python3 dpdk-hugepages --pagesize 1G --setup 2G # 这是设置单页大小为1G，总计2G的大页内存，共计2页

# 大页内存绑定完了，接下来就需要绑定网卡了，因为之前的部署经验，强烈建议使用vfio驱动，
# uio驱动在dpdk21LTS的RM上还可以用，但不知道dpdk23LTS还可不可以(大概率不行)。
#modprobe uio
modprobe uio_pci_generic # 不建议使用uio
# 如果您使用虚拟机测试开发，那么请注意VMXNET3虚拟网卡貌似不支持vfio驱动，这个时候可以用uio驱动
#modprobe vfio
#modprobe vfio-pci
#modprobe vfio_iommu_type1
ifconfig ens192 down # 这里的ens192就是服务器虚拟机第二块网络适配器，PCIE直通的那块，ens160保留给虚拟机，而且ens160是虚拟网卡，不能用来插网线
python3 dpdk-devbind.py -b uio_pci_generic ens192
#python3 ./dpdk-devbind.py -b vfio-pci ens192 # 不建议使用uio

