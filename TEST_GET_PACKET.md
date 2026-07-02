# GET 包匹配测试说明

## 测试文件说明

### 1. `test_get_packet.py` - 基础 UDP 测试
使用标准 socket 发送 UDP 格式的 GET 包，适合简单测试。

**使用方法:**
```bash
# 默认参数 (127.0.0.1:12345)
python test_get_packet.py

# 指定服务器地址
python test_get_packet.py 192.168.1.100 54321
```

**包含的测试场景:**
1. **匹配成功测试** - 使用已注册的 SID
2. **SID 不存在测试** - 使用不存在的 SID
3. **校验和错误测试** - 发送校验和错误的包
4. **SID 列表请求测试** - 使用全 0 SID 请求列表

---

### 2. `test_get_with_scapy.py` - 完整以太网帧测试
使用 Scapy 构造完整的 Ethernet + IPv4 + UDP + GET 数据包，更接近真实的 DPDK 流量。

**安装依赖:**
```bash
pip install scapy
```

**使用方法:**
```bash
# 需要 root 权限发送原始数据包
sudo python test_get_with_scapy.py --iface eth0 --dst-mac 00:11:22:33:44:55
```

**参数说明:**
- `--iface`: 发送数据包的网卡接口
- `--dst-mac`: 目标 MAC 地址 (RM 服务器网卡 MAC)
- `--src-mac`: 源 MAC 地址 (可选)
- `--src-ip`: 源 IP 地址 (可选)
- `--dst-ip`: 目标 IP 地址 (可选)

---

## GET 包格式说明

### GET_front_t 结构 (72 bytes)
| 字段 | 大小 | 说明 |
|------|------|------|
| version_package | 1 byte | 版本 + 包类型 (0x72) |
| ttl | 1 byte | 跳数限制 |
| pkg_len | 2 bytes | 包总长度 |
| checksum | 2 bytes | 校验和 |
| MTU | 2 bytes | MTU |
| PID_num | 1 byte | PID 数量 |
| flag | 1 byte | 标志位 |
| Minimal_PID_CP | 2 bytes | 最小路径 CP |
| N_sid | 16 bytes | 服务标识符 |
| L_sid | 20 bytes | 定位标识符 |
| nid | 16 bytes | 节点标识符 |
| PID[] | 4*N bytes | 路径标识符 (可选) |

---

## 匹配成功 vs 失败

### 匹配成功条件
1. ✅ 校验和正确
2. ✅ N_sid + L_sid 在 RM 的 SID 表中存在匹配项
3. ✅ 策略过滤通过 (如果启用)

**匹配成功后的行为:**
- 如果提供者在本域: 转发给终端
- 如果提供者在其他域: 转发给边界路由器 (BR)
- 对应计数器: `to_inside_count` 或 `to_outside_count` 增加

### 匹配失败条件
1. ❌ 校验和错误 → `filtered_count` 增加
2. ❌ SID 不在表中 → `filtered_count` 增加
3. ❌ 策略过滤不通过 → `filtered_count` 增加

**匹配失败后的行为:**
- 数据包被丢弃
- `filtered_count` 统计计数增加

---

## 如何验证测试结果

### 方法 1: 观察 RM 终端输出
运行 RM 服务器时，观察控制台输出：
```
Receive a GET packet
The requested content is in this domain: ...
```

或者：
```
ERR: checksum failure
```

### 方法 2: 查看统计计数器
在 CLI 中查看状态信息，观察 `GET_stat` 统计：
- `filtered_count`: 被过滤的包数 (匹配失败)
- `to_inside_count`: 转发到本域的包数 (匹配成功)
- `to_outside_count`: 转发到其他域的包数 (匹配成功)

---

## 测试建议

1. **配置 SID 表:**
   首先确保 RM 中已注册了一些 SID，可通过 CONTROL_CONFIG_RM 命令配置

2. **调整测试用的 SID 值:**
   修改测试脚本中的 SID 值，使其与实际配置匹配

3. **抓包验证:**
   使用 Wireshark 或 tcpdump 抓包，验证数据包格式

4. **单独测试每个场景:**
   每次只运行一个测试场景，便于观察对应计数器的变化

---

## 常见问题

**Q: 为什么发送的包没有反应？**
- 检查 DPDK 是否绑定了正确的网卡
- 确认目标 MAC 地址正确
- 检查 RM 是否正常运行

**Q: 校验和总是失败？**
- 确认校验和计算逻辑正确
- 检查字节序 (大端/小端)
