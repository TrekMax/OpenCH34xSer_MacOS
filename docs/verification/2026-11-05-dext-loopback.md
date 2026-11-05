# 2026-11-05 DEXT 第 4 路回环

## 条件与 build 5 结果

用户确认第 4 路 TX/RX 短接，允许发送测试数据。测试只使用逻辑端口 3 的标准 `cu` 节点，配置为 8N1，不通过 libusb 绕过 DEXT。

- 115200：1、16、509 字节逐字节回环通过，RX 日志为 1、16、480 + 29 字节，驱动未崩溃。
- 9600：1、16、30、31、32、64、255、509、510、1024 字节通过；连续 4096 字节两次出现等长但内容不一致。
- 第二次 4096 字节错误位置为偏移 15～23，共 9 字节。期望/接收二进制已留存。
- 分片等待回环的对照：前 5 片各 509 字节正确，随后停止发送；重新打开后 1 字节测试也未收到回包。该对照不算 4096 字节通过。

旧版本仅在配置时同步清空 command IN；运行时不接收状态，USB OUT 完成后立即发送下一片。[WCH 官方协议实现](https://github.com/WCHSoftGroup/ch9344ser_linux/blob/main/driver/ch9344.c) 的 `ch9344_tty_write` 则等待每路 `write_empty`，由 `ch9344_cmd_irq` 解析 IIR 低四位为 `0x02` 的通知恢复。该差异为本轮修复假设，须以实际回环复测判定效果。

## build 6 改动与主机验证

- 独立缓冲区持续异步读取 command IN；运行时配置不再同步吞掉状态。
- 每路发送一片后，等待该路发送空闲通知；USB 完成仍负责提交 TTY 消费位置。
- 状态解析跳过完整配置回复，拒绝截断/未知记录，避免将回复内容误认作发送空闲。
- 退出时先标记 inactive，再中止状态读取并释放资源。
- 新增状态解析、端口隔离和重复通知测试；主机 CTest 8/8 通过。
- 双架构 Debug 编译及实际签名/profile 校验通过，已安装并提交激活。首次激活后系统仍显示 build 5 `terminating for upgrade via delegate`，等待 USB 重新插拔切换到 build 6 后复测。

硬件回归脚本已扩展为 9600/115200 的边界长度、4096 字节连续发送，以及 115200 的 65536 字节全双工回环：

```bash
python3 tests/driverkit/test_port4_tty_loopback.py
```

仅在第 4 路短接、无外部业务设备连接时运行。各版本实际结果见后文；原旧节点残留问题仍待独立验证。

证据目录：`build/loopback-2026-11-05/`，包括 `matrix-repro.log`、`expected-9600-4096.bin`、`received-9600-4096.bin`、`after-paced.log`、`status-red.log`、`build6.log`、`core-tests.log`。

## 03:18 重新接入：升级阻塞

- USB 设备已重新枚举为 `0x10005d481`，VID/PID 仍为 `1a86:e018`。
- 原四个串口服务、八个旧设备节点均消失；旧活动 transport/device 进程已退出。这一轮含 RX 后拔出完成了节点清理，但不覆盖用户最初的全部复现条件。
- build 5 还有 PID 52697 残留，归属 `_driverkit`。其服务 `0x10005d26b` 在 03:04:34 的 Start 已失败，此后从未退出，与刚拔出的活动 transport `0x10005d27d` 不同。
- `systemextensionsctl` 中 build 5 仍为 `terminating for upgrade via delegate`；build 6 虽为 `activated enabled`，尚未有实际进程。
- kernelmanagerd 仍按旧 unique ID 选择 build 5；launchd 执行旧路径时报 `Permission denied`，因此无串口节点。不能把该状态算作 build 6 收发失败或成功。
- 经管理应用发出的停用请求被系统以 OSSystemExtensionErrorDomain Code 13 拒绝。当前会话 `sudo -n` 也要求密码，已请用户在终端结束残留旧进程后继续。

证据：`build6-replug.log`、`upgrade-block.log`、`old-failed-start.log`。Start 失败分支未调用父类 Stop 是后续清理审计候选，尚未通过故障注入确认。

## 08:35 授权后状态

- 用户完成系统授权后，build 6 进入 `terminating for uninstall but still running`；旧 build 5 的 PID 52697 仍存活。
- 已通过 `/Applications/OpenCH34xSerHost.app` 重新提交 build 6 激活。应用明确显示“重启后完成驱动激活”，系统列表为 `activated waiting to upgrade on reboot`。
- 活动监视器中对 PID 52697 执行正常退出与强制退出后，进程仍存在；未观察到退出成功。当前终端的 `sudo -n` 仍要求密码。
- 已安装应用的签名/profile 校验仍通过；旧节点全部消失，目前没有可用的 CH9344 串口，尚不能运行 build 6 回环测试。
- 下一步需要正常重启 macOS 后确认实际运行的 DEXT 版本与四路节点，再在保持第 4 路短接的条件下运行长包测试。

证据：`build6-recovery.log`、`build6-awaiting-reboot.txt`。

## 08:49 重启后 build 6 实测与 build 7

- 重启后只有 build 6 处于 `activated enabled`，实际进程来自 `7F147BC8-BDED-4A16-8212-A69B54EDB182` 安装目录，bundle 版本和签名均已核对。旧 build 5 进程已消失。
- 四路名称与旧节点清理检查通过。第 4 路为 `/dev/cu.serialCH93xxUSB3-100001554`。
- 9600 和 115200 各自的 1、16、30、31、32、64、255、509、510、1024、4096 字节均逐字节通过。build 5 的 9600/4096 内容错误在这一轮已消除。
- 115200 的 65536 字节测试超时：应用写入 17306 字节、收到 509 字节。日志明确为 `TX prepare failed 2 pi 30028 ci 13645 logsz 14`，即累计 producer 超过 16384 后被当作非法下标。
- build 7 修正 TTY 环形队列：保留累计的 uint32_t producer/consumer，使用计数差检查占用量，仅在访问内存时按 ring size 取模。TX 和 RX 均采用同一规则。
- 回归用例包括上述真实计数、跨物理缓冲区末尾、跨 uint32_t 回绕及背压；修改前失败、修改后通过。完整主机 CTest 8/8、双架构 Debug 编译与签名/profile 校验通过。
- build 7 已安装并提交激活，等待 USB 重新插拔后实际接管；64 KiB 与连续收发仍待复测。

证据：`build6-reboot-loopback.log`、`build6-reboot-transport.log`、`counter-red.log`、`build7.log`、`build7-core-tests.log`、`build6-nodes.json`。

## 08:52 build 7 实机通过

- 重新插拔后 build 6 完成退出，系统仅保留 build 7 `activated enabled`。实际两个进程的 bundle 版本均为 7，目录为 `91B7BDE9-5C4A-4C31-9AB6-2DAAB43CB9E3`。旧节点快照检查通过。
- 第 4 路 `/dev/cu.serialCH93xxUSB3-100001B28`，其他逻辑端口 0/1/2 的后缀为 `100001B23`、`100001B24`、`100001B26`。
- 标准回归脚本 23 个用例全部通过：9600/115200 各 11 个边界长度，外加 115200 的 65536 字节。此前相同 64 KiB 用例的停发已不再出现。
- 921600 8N1：1、30、31、509、510、4096、65536、1048576 字节伪随机数据均逐字节相同。1 MiB 耗时 11.428 秒，接收 SHA-256 为 `8ae9c28d58968c625c8f07cf12ff3818683f6228799df0707e389ce37f61d21f`。
- 随后关闭/重开第 4 路 10 次，每次 115200、509 字节随机回环均通过；无多余接收字节，节点未重枚举，驱动 PID 3161/3162 保持不变。
- 测试期间未见新增 `TX prepare failed`、状态解析失败或 USB 提交/完成失败日志。环形队列 ASan/UBSan 测试通过。
- 安装应用签名/profile、扩展激活、4 对串口归属检查通过。保留用户原暂存区，未提交或推送。

证据：`build7-loopback.log`、`build7-high-speed.log`、`build7-high-speed-results.json`、`build7-traffic.log`、`build7-nodes.json`。高速测试脚本为证据目录中的 `high-speed-loopback.py`，随机种子为 `0x9344`；任意波特率通过本机 SDK 的 IOSSIOSPEED ioctl 设置。

边界：仅第 4 路进行了物理 TX/RX 回环；不代表其他三路同时通信、长期稳定性、build 7 重启恢复或历史旧节点残留问题已完全验收。

## 当前安装包

- DMG：`build/packaging-2026-11-05/OpenCH34xSer-0.1.0-build7-development.dmg`。
- SHA-256：`26b8be2ddad6a167c60d2511aa874e37beaa59d0f0b760ef9084815e6602edeb`。
- 镜像校验通过；只读挂载后签名/profile 校验通过，Host/DEXT 可执行文件 SHA-256 与 `/Applications` 已安装版本一致；验证后已卸载镜像。
- Apple Development 开发包，非 Developer ID 公证公开分发包。

## 08:56 补测 12 Mbps

[WCH 官方资料](https://www.wch.cn/downloads/CH9344DS1_PDF.html) 标明 CH9344 支持最高 12 Mbps。当前驱动已接受 12000000 波特率，CH9344Q 使用直接编码的 32 位波特率；此前 921600 是实测覆盖范围，并非驱动上限。本轮未修改驱动或安装包。

- 仍使用 build 7 第 4 路 `/dev/cu.serialCH93xxUSB3-100001B28`，TX/RX 短接，通过 IOSSIOSPEED 配置 12000000、8N1。
- 1、30、31、509、510、4096、65536、1048576 字节随机回环全部逐字节一致。
- 1 MiB 耗时 0.951 秒，应用有效载荷吞吐约 1.10 MB/s；12 Mbps 的 8N1 理论有效载荷上限为 1.20 MB/s。该测量包含系统/USB/分片等待开销，未用示波器单独测量 TX 位宽。
- 1 MiB 接收 SHA-256：`8ae9c28d58968c625c8f07cf12ff3818683f6228799df0707e389ce37f61d21f`。
- 随后恢复 115200 并重开收发 10 次均通过；驱动 PID、四路节点保持不变，无额外接收字节。

证据：`build/loopback-2026-11-05/build7-12mbps.log`、`build7-12mbps-speed-results.json`；复测脚本为同目录 `12mbps-loopback.py`。只验证了第 4 路，未进行四路同时 12 Mbps 验收。
