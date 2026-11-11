# 欢迎使用资源管理器RM

## 一、前期准备

### 1.dpdk环境配置

资源管理器运行在dpdk23LTS上，首先需要安装dpdk23LTS版本，以ubuntu jammy操作系统为例

dpdk只需要numa库依赖，其余依赖大部分都是build工具

```bash
apt update
apt install build-essential pkg-config python3-pyelftools meson ninja-build net-tools libnuma-dev
```

然后进入dpdk源码目录，执行：

```bash
meson setup build # 如果需要把dpdk官方自带样例也编译了就执行meson setup -Dexamples=all build
meson compile -C build
meson install -C build
ldconfig
```

如果需要卸载dpdk，或者更换dpdk版本，请在dpdk源码目录下执行：

```bash
cd build
ninja uninstall
```

### 2.RM编译准备

获取到RM源码，本readme所在的目录即为项目根目录

```
config_receive_file # 一个控制报文二进制文件，用于初始化配置RM的BR信息，该文件可以通过脚本as-config.py生成
rm_config # 一个文本文件，用于给RM的端口设置ip地址，目前RM只用一个端口（Port 0）
re-build-rm.sh # 修改了RM的源码后，请使用该脚本重新编译RM
set-rm.sh # 每次开机的时候，执行一次这个脚本，帮您自动配置dpdk的大页内存和网卡驱动绑定，每次开机/重启执行一次就可以了
run-rm.sh # 运行RM，需要关闭RM软件时按Ctrl+C退出
```

编译选项 `idle_sleep_us`（`meson_options.txt`，默认 0）：worker 连续空转若干轮后睡这么多微秒。真机保持 0（忙轮询，与原来一样）；容器实验环境（color-lab）用 `meson setup build -Didle_sleep_us=50`，让空闲的 RM 不占满 CPU，代价是空闲后第一个包多几十微秒时延。

## 二、RM的使用

每次开机/重启设备时，执行```set-rm.sh```脚本。

执行```run-rm.sh```即可运行RM，关闭时按Ctrl+C退出。

如您修改了RM源码，请执行```re-build-rm.sh```

启动后，使用help命令查看RM的使用帮助命令，目前主要是在RM上增加了ACL访问控制列表功能。

运行期间可使用 `display status` 查看 ANN、GET 和 Proxy 的累计统计。统计从零开始，
内部使用 64 位计数器；原有 CONTROL 状态响应继续保留 32 位兼容格式。

`display sid` 列出全部 SID。提供端在 ANN 里带了名称（策略 tag 13）和描述（tag 14）时一并显示：
名称是 L_SID 的哈希原文，RM 会校验 SHA1(名称) = L_SID，对不上的名称前面标 `?`，只能参考；
描述是提供端自报的协议名，只用于展示。格式见 `docs/design-ann-sid-desc.md`。

RM 的运行事件会异步写入结构化日志，默认文件为当前目录下的 `rm.log`。单个日志文件
达到 10 MiB 后自动轮转，最多保留 5 个历史文件。可以在启动前通过以下环境变量调整：

```bash
RM_LOG_FILE=./rm.log          # 日志文件路径；设置为 none/off 可关闭文件输出
RM_LOG_LEVEL=info             # debug/info/warn/error，默认 info
RM_LOG_STDERR=1               # 是否同时输出到 stderr，默认 1
RM_LOG_ROTATE_SIZE=10485760   # 轮转阈值，单位字节，默认 10 MiB
RM_LOG_ROTATE_FILES=5         # 历史文件数量，默认 5
```

高频异常事件会被限速，日志队列满时会丢弃日志并记录丢弃计数；完整流量数量以状态
计数器为准。`display status` 中还包含总 RX/TX、非 IPv4、非 CoLoR、未支持类型和
TX 丢弃等运行指标。

### 演示开关：攻击者 AS

查不到 SID 的 GET 默认直接丢弃（计入 `sid_not_found`）。演示「攻击者 AS 发出非法 GET」时，
可以在那台 RM 上设置：

```bash
RM_DEMO_FORWARD_UNKNOWN_SID=1   # 只认 1；查不到 SID 的 GET 改为转给第一个邻居 AS 的第一个 BR
```

这个行为以前写死为「aid 为 1 的 RM 一律开启」。在 AS1 是正式域的部署里，它会把查不到
SID 的 GET 转出本域，而且每个这样的 GET 都会泄漏一块内存，所以现在改成显式开启。
正式部署不要设置这个变量。

### Web 可视化监控

RM 默认启动 Web 监控服务，监听 `127.0.0.1:8080`，浏览器打开该地址即可查看
RX/TX 趋势、关键计数器和运行日志，并管理 SID 列表与 ACL 访问控制规则
（ACL 支持在线添加/删除）。生产环境建议通过反向代理或安全隧道对外提供访问，
注意 ACL 写接口会直接修改运行时规则。

启动前可通过以下环境变量调整：

```bash
RM_WEB_ENABLED=1             # 是否启用，设置为 0/off 可关闭
RM_WEB_BIND=127.0.0.1        # IPv4 监听地址
RM_WEB_PORT=8080             # 监听端口，设置为 0 可使用临时端口
RM_WEB_ROOT=./web            # Dashboard 静态资源目录
```

提供以下接口：

```text
GET /api/v1/health                 服务健康状态
GET /api/v1/metrics                JSON 运行指标、端口状态和日志统计
GET /api/v1/logs?after=0&limit=100 JSON 增量日志，limit 最大 256
GET /api/v1/sids                   JSON 系统全部 SID 列表，每条含 sid、name、name_verified、desc
GET /api/v1/acls                   JSON 当前 ACL 规则列表
POST /api/v1/acls                  添加 ACL 规则，JSON 体 {id, action, sid, asid, nid}
DELETE /api/v1/acls?id=N           删除指定 ID 的 ACL 规则
GET /metrics                       Prometheus 文本格式指标
```

安装时 Dashboard 会放到 `${prefix}/share/rm/web`；从安装目录运行时设置
`RM_WEB_ROOT=${prefix}/share/rm/web`，如果从源码构建目录直接运行，保持 `RM_WEB_ROOT=./web`
即可。服务启动后可运行 `bash tests/web_smoke.sh` 做接口冒烟检查，
也可以通过 `RM_SMOKE_URL=http://127.0.0.1:8080 bash tests/web_smoke.sh` 指定地址。

### 注意

如果发现脚本不能运行，可能是权限问题

对不能运行的脚本，执行

```
chown -R <您的ubuntu用户名>:<您的ubuntu用户名> ./xxx.sh
chmod +x ./xxx.sh
```

如果您的设备显示无vfio驱动，您需要打开```/etc/default/gub```文件，然后找到这样一行```GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"```，把它改成```GRUB_CMDLINE_LINUX_DEFAULT="quiet splash intel_iommu=on iommu=pt"```

然后执行

```
update-grub
reboot
```

如果虚拟机或VMWare ESXi环境不支持vfio驱动，那么请确保dpdk版本在23LTS，使用uio驱动。

