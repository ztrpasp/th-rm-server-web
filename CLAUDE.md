# CLAUDE.md

共生网络（CoLoR）的 **RM（资源管理器）**，C + DPDK 23 LTS。职责：终端注册；处理 ANN，维护 SID 路由表；
处理 GET（查表、ACL、策略过滤、追加 PID 后转发）；向 BR 同步控制信息。在原版 RM 基础上加了 Web 监控、结构化日志和计数器。

## 先读

- `docs/HANDOVER-rm.md`：项目全貌、报文流程、代码导览、部署拓扑、真机踩过的坑、backlog。**新会话先读它。**
- `docs/design-ann-sid-desc.md`：当前任务的方案（ANN 携带 SID 名称/描述，策略 tag 13/14）。
  原件在 color-proxy 仓库（`C:\Users\ZWD15\Desktop\color-proxy\docs\`），两份不一致时以原件为准。
- 协议原文：`../参考资料/共生网络技术文档+报文转发（修正）.docx`。
- 相关仓库：BR 在 `../the-new-br-master`，终端代理在 `C:\Users\ZWD15\Desktop\color-proxy`；
  `../the-rm-server`、`../the-new-rm-master` 是旧版 RM，只作对照。

## 构建与测试

只能在 Linux（RM 机器）上编译运行，Windows 上只改代码。**改好的代码怎么同步到 RM 机器，先问用户。**

| 做什么 | 命令 |
|---|---|
| 开机初始化（大页内存 + 绑网卡），每次开机一次 | `./set-rm.sh` |
| 重新编译（会先删掉 `build/`） | `./re-build-rm.sh`（即 `meson setup build && meson compile -C build`） |
| 运行（Ctrl+C 退出） | `./run-rm.sh` |
| C 单元测试 | `meson test -C build` |
| Web 接口冒烟 | `bash tests/web_smoke.sh`（`RM_SMOKE_URL` 指定地址） |
| 手工发包 | `tests/scapy/`：从另一台机器往 RM 网卡发 ANN/ARP/DATA，网卡名和 MAC 写死在脚本里，用前先改 |

- 测试都放在 `tests/`：C 单元测试写成 `tests/test_*.c` 并在 `tests/meson.build` 注册；发包脚本放 `tests/scapy/`。不要在根目录散放测试文件。
- 编译选项 `-Wall -Wextra`，静态链接。
- 工作区 `core.autocrlf=true`，仓库存 LF。绕过 git 直接把文件拷到 Linux，`*.sh` 会带 CRLF 跑不起来。

## 代码结构

收包入口 `include/process/rm_main_function.h` 的 `rm_process_single_packet`：ARP 交给 `arp.c`；
非 IPv4、IP 协议号不是 150、CoLoR 版本不是 7 的丢弃；再按类型分发（ANN=1、GET=2、CONTROL=4），DATA（3）丢弃。

| 路径 | 内容 |
|---|---|
| `src/app/` | `main.c`、`init.c`：EAL/端口/内存池初始化、lcore 分配、读配置文件 |
| `src/packets/` | 四类报文入口 `ann.c`、`get.c`、`ctrl.c`、`arp.c`；`rte_CoLoR.c` 是报文构造、校验和、打印、md5 |
| `src/process/rm_pkt_process.c` | **核心逻辑**：ANN 解析/存表/转发/重组，GET 查表/ACL/策略过滤/追加 PID，CONTROL 解析 |
| `src/process/rm_send.c` | 组 IP 头发包；ARP 未解析时缓存待发包 |
| `src/tables/` | SID 表（`rm_sid_table.c`，含给 Web/CLI 用的平铺表 `g_tsid_table`）、终端/BR/邻域 AS 表、拓扑图（`rm_as_graph.c`，`graph_trace`）、ACL、静态路由、ARP、`my_as_info` 与全局策略 |
| `src/telemetry/` | 结构化日志（`rm_log.c`）、计数器、监控快照、Web（`rm_web.c`，静态页面在 `web/`） |
| `src/commandline/` | CLI：`display [config\|graph\|acl\|sid\|status]`、`acl add …`、`acl delete id N` |
| `src/vendor/` | 第三方：cuckoo hash、红黑树、`rte_array` |
| `color_packet/`、`as-config.py` | Python 工具：生成 `config_recieve_file`（CONFIG_RM 控制报文） |

配置文件（运行目录下）：`rm_config`（端口 IP）、`config_recieve_file`（本域/邻域/BR/PX，文件名拼写就是这样，别改）、
`global_as_config`（全局拓扑，ACL 依赖它）、`ip_route_static`（静态路由）。

## 改代码时注意

- **线程**：数据面单线程（worker lcore 上的 `worker_main`），但 CLI（主 lcore）、Web（pthread）会和它并发读写同一批表，**没有加锁**。
  给 Web/CLI 看的数据存副本（如 `g_tsid_table`），不要让它们直接读数据面会释放的结构，否则会 use-after-free。
- **不可信输入**：终端和邻域发来的字段（ANN 的 TLV、名称、描述）解析时必须做边界检查；输出到 JSON 要转义
  （`src/telemetry/rm_monitor.c` 的 `json_append_string`）。
- **字节序与校验和**：注意 CoLoR 头里的小端字段（例如 ANN 的 `pkg_len`）；重算 IPv4 校验和前先把校验和字段清零。
- **GET 的 PID**：用 `rm_GET_pid_start`（从包尾倒推）定位。终端的 GET 带 4 字节 Segment_ID，固定偏移是错的。
- **策略 tag**：1–12 已占用（1、2 随 ANN 携带，3–12 是 RM 全局开关，共用一套编号），新 tag 从 13 起，
  不要超过 30（`global_quick_set_strategy` 用 `1 << t`）。
- **ACL**：GET 路径上每条链路都要在 `global_as_config` 里，`graph_trace` 倒推源 AS 失败时一律拒绝。
- 改了 CLI、Web 接口或环境变量，同步更新 `README.md`。
- 报文格式、tag 号等跨仓库契约以 color-proxy 的 `docs/` 为准；两边对不上时先停下来问用户。

## 当前状态（2026-10-29，分支 `dev_zwd`）

- 正在做：`docs/design-ann-sid-desc.md` §4，按 P0（缓冲区溢出、解析边界、未初始化读）→ P1（TSID 存名称/描述、内嵌 SHA1 校验、
  Web/CLI 展示、-2 日志降级）→ P2（可选，按名称前缀写 ACL）的顺序，每档单独提交。自测用方案 §2.6 的 92 字节 ANN 和 SHA1 向量。
- `925eb57`（IPv4 校验和清零）和 `c598cb8`（查不到 SID 的 GET 默认丢弃）还没上真机验证。
- `origin/dev` 上有 4 个与 `dev_zwd` 无关的提交，别混用。

## 工作约定

- 用中文交流；先说结论，少用自造术语。
- 可以按判断 commit（大任务分步提交）；push、改写历史要先问。
