# 2026-11-05 拔插残留与串口命名排查（进行中）

## 已确认的问题

用户在 build 3 观察到重新插拔后仍有 8 对串口节点。实机 IORegistry 复现：旧的四个 CH9344Driver 处于 `IOServiceState=1`、busy=2；旧 transport 为 state=1、busy=5，而新四个端口 state=30、busy=0。旧 transport 进程 PID 33360 当时仍存在，lsof 未发现串口被用户进程打开。这说明旧服务终止没有及时完成，不能将本问题归因于终端缓存，也不能只用新端口出现判定拔插验收通过。

原退出流程：Transport::Stop -> TerminateSerialPorts -> DeactivateTransport（同步 Abort、释放资源、关闭 interface）-> super Stop。原构建缺乏阶段日志，尚未确定阻塞在哪一调用；未贸然修改 Abort 或资源释放次序。进程采样因系统权限不足未取得（sudo 需要密码）。后来旧服务全部消失，同时设备离线，不能据此判定修复。

## build 4 诊断改动

- 在 CH9344Driver::ConfigurePort 中，于 super Start 之后、RegisterService 之前调用 SetProperties，设置 `IOTTYBaseName=serialCH93xxUSB{0..3}-` 与大写 registry ID 后缀。目标为 `/dev/tty.serialCH93xxUSB0-<ID>`，callout 对应 `/dev/cu.serialCH93xxUSB0-<ID>`。
- 记录命名调用返回码，以及 Stop、各子端口 Terminate、IN/OUT Abort 的进入/返回阶段，以确定实际阻塞位置。
- 新增 test_serial_lifecycle.py：连接状态必须恰好有 4 个活动服务且名称匹配；拔出后必须为 0，传入旧快照时检查旧字符设备路径已不存在。
- serial_nodes.py 支持 ioreg 在没有服务时输出空内容，并明确区分“重复逻辑端口可能是多个设备或旧服务残留”。

**命名已通过实机验证；收发后的残留问题仍在排查。** 2026-11-05 02:54 的 attached 检查同时确认 IOSerialBSDClient 和 /dev 字符设备的实际名称为 `serialCH93xxUSB0..3-<ID>`，不是只检查 SetProperties 返回码。

## 当前状态

- build 4 双架构 Debug 签名构建成功，应用已复制到 `/Applications/OpenCH34xSerHost.app`。
- 2026-11-05 01:03 左右，GUI 确认激活成功；系统扩展版本以 systemextensionsctl 最终输出为准。
- 默认 8/8 CTest 通过；实际 Host/DEXT 签名、profile 验证通过。
- 设备目前不在 IOUSB 列表中，不能执行连接/拔插检查。未连接时 detached 检查通过仅证明当前没有服务，不证明旧残留问题已修复。
- 真实 TX/RX 回环继续保持用户要求的待验证状态。
- 未提交 Git，保留先前暂存改动。

## 后续实机步骤

连接 CH9344 后：

```bash
python3 tests/driverkit/test_serial_lifecycle.py attached \
  --snapshot build/hotplug-2026-11-04/nodes-before-unplug.json
```

拔下设备并保持断开后：

```bash
python3 tests/driverkit/test_serial_lifecycle.py detached \
  --previous build/hotplug-2026-11-04/nodes-before-unplug.json
```

再次接入后运行 attached，并带同一 `--previous` 验证旧路径未残留。若拔出失败，读取阶段日志确定阻塞点后再修复。

证据目录：`build/hotplug-2026-11-04/`。其中 `CH9344*-before.plist` 保存 8 个端口时的状态；`build4.log` 为编译日志。默认测试日志位于 `build/hotplug-verification/Testing/Temporary/LastTest.log`。

## 02:54 后实机结果

- 初次接回：4 个活动串口服务，8 个字符设备，逻辑端口 0～3 的名称均匹配。
- 打开逻辑端口 0、设置 115200 8N1、保持打开但不发送数据；用户于 02:55:59 拔下设备。
- 拔出日志中四个子端口 Terminate 和 Stop 均返回成功，Transport 到达 USB deactivated；驱动进程退出。
- detached 与旧路径快照检查通过：0 个串口服务，全部 8 个旧路径消失。
- 再次接回：attached 与旧路径检查通过，仍只有 4 个活动服务；新后缀为 10005D059、10005D05A、10005D05B、10005D05E。
- 用户补充：最初出现残留之前运行过收发程序。因此无收发拔插通过不能代表原问题已修复，下一步针对收发后退出路径复现。

证据：`build4-connected.log`、`build4-unplug-open-idle.log`、`nodes-before-unplug.json`、`nodes-after-replug.json`。

## build 5 与发送后拔出

- 在 build 4 的空闲第 4 路发送 `CH9344-TX-CHECK\r\n` 共 17 字节，tcdrain 与 close 均返回；该测试证明发送队列排空和驱动未崩溃，不证明物理 TX 波形或对端接收正确。
- 发送后拔出，旧路径检查通过，所有驱动进程退出。
- build 5 仅增加 TxFreeSpaceAvailable / RxDataAvailable 调用前后日志，仍未修改退出行为。它已签名安装并激活。
- 用户运行原收发程序后于 03:01:34 拔出。日志中有 14 对 TX 完成通知 begin/end，四个端口 Stop 均成功返回，transport 到达 USB deactivated。
- detached 与旧快照检查通过：0 个串口服务，8 个旧设备路径全部消失。状态采样在 03:01:35 已记录为空。
- 本轮没有 RxDataAvailable 记录，因此不能宣称 RX、完整回环或原残留问题修复。仍需确认程序是否收到回包、拔出时是否仍在收发。

证据：`build4-unplug-after-tx.log`、`build5.log`、`build5-traffic-stream.log`、`build5-traffic-states.jsonl`、`build5-before-traffic.json`。

## 03:03 交付（build 5 历史状态）

- 已安装并激活 build 5；没有修改既有 Stop/Terminate/Abort 行为，保留诊断日志。
- 默认 CTest 8/8、应用签名/profile、DMG 校验与挂载后签名检查均通过。
- DMG：`build/packaging-2026-11-05/OpenCH34xSer-0.1.0-build5-diagnostic.dmg`。
- SHA-256：`febadbd110804adbcb6d7c8e71927c255ad3189c341b3b3f48ccd04b42c231b7`。
- 名称修改已实测；原残留问题在三种测试条件下未复现，根因仍待确认。

后续已安装 build 7，并完成第 4 路真实长包回环。build 6 回环后拔插，旧节点和两个活动驱动进程均退出，build 7 正常接管。最终收发与安装包状态见 [DEXT 回环记录](2026-11-05-dext-loopback.md)；原 build 3 残留及 Start 失败进程的完整修复仍不在本轮通过范围内。
