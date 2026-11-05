# 2026-11-04 签名、安装与实机排查

## 当前结果

已安装双架构 Debug build 3（0.1.0/3），使用 Apple Development 证书。宿主为 `/Applications/OpenCH34xSerHost.app`，GUI 显示“驱动已激活”；`systemextensionsctl list` 确认 Team `9BT9NV52NL` 为 `activated enabled`。

用户于 20:52 重新插拔设备后，驱动自动配置 USB 并恢复四路串口。日志证据：

```text
20:52:15.017 IOUSBHostDevice::setConfigurationGated: CH9344Device selected configuration 1
20:52:15.018 CH9344Device configuration 1: 0x0
20:52:15.018 CH9344Device::start(...) ok
```

IORegistry 同时存在 `CH9344Device`、`CH9344Transport`、4 个 `CH9344Driver`（内核代理类为 `IOUserSerial`），每个代理下有一个 `IOSerialBSDClient`。本次重新插拔后的映射如下，重新枚举后后缀可能变化：

| 逻辑端口 | callout | dial-in |
| --- | --- | --- |
| 1 | `/dev/cu.serial-10005A494` | `/dev/tty.serial-10005A494` |
| 2 | `/dev/cu.serial-10005A495` | `/dev/tty.serial-10005A495` |
| 3 | `/dev/cu.serial-10005A496` | `/dev/tty.serial-10005A496` |
| 4 | `/dev/cu.serial-10005A499` | `/dev/tty.serial-10005A499` |

四路均通过标准 TTY 打开、9600/115200 8N1 设置与回读、恢复和关闭；没有发送测试数据，检查期间节点未重新枚举。

**待验证：新版 DEXT 的真实 TX/RX、回环、TX 崩溃修复效果、长时间稳定性和重启恢复。** 用户确认第 4 路未短接，并要求保留待验证状态。此前 libusb 回环和本次配置成功均不能替代 DEXT 收发验收。最初尝试 921600 配置检查时，当前 Python `termios` 无 `B921600` 常量；随后正式检查限于 9600/115200，未将 921600 计为通过。

## 定位与修复

### 1. 系统扩展激活成功不等于运行时签名通过

最初 build 1 使用具体 VID/PID 的 USB entitlement，与开发 profile 的 `[{idVendor: "*"}]` 不一致。Xcode 构建及 codesign 完整性检查成功，系统扩展也显示 active/enabled，但实际启动被 AMFI 拒绝：

```text
Unsatisfied Entitlements: com.apple.developer.driverkit.transport.usb
kernelmanagerd: Exec format error
```

新增 `CH9344Driver.development.entitlements` 并仅用于 Debug，与开发 profile 保持一致；Release 保留具体 VID/PID。设备和接口 personality 仍仅匹配 `1a86:e018`，并未扩大实际设备匹配范围。build 2 已实际启动并发布四路串口。

参考 [Apple DTS: How to sign a DEXT](https://developer.apple.com/forums/thread/809202)：开发与发布 entitlement 应分开配置，Xcode 27 的构建检查不能替代运行时匹配。新增 `test_signed_bundle.py` 检查实际签名、嵌入 profile、受限 entitlement、Team 一致性及有效期；旧 build 1 因 USB entitlement 不匹配失败，新 build 3 通过。

本记录修正了第一轮“签名已排除、只需排查接口”的过早结论。

### 2. Vendor-specific USB 设备没有自动选择 configuration

初始 IORegistry 只有 `IOUSBHostDevice`，没有 interface。一次仅针对当前 CH9344 的本机诊断调用 `configureWithValue:1 matchInterfaces:YES` 后，接口立即出现，暴露了上述 AMFI 拒绝。

永久修复为新增 `CH9344Device` 的设备级 personality，匹配 VID/PID，打开设备并调用 `SetConfiguration(1, true)`，再由原 `CH9344Transport` 独占 interface 0 和四条 pipe。build 3 已通过用户实际拔插验证，不再依赖诊断程序。

### 3. TX 缓冲区映射引起写入崩溃

20:48:01 的 build 2 崩溃报告为 `EXC_BAD_ACCESS / KERN_PROTECTION_FAILURE`，写入异常堆栈为：

```text
encodeTxFrame (Protocol.cpp:44, output[0] = hardwarePort)
DriverCore::prepareTxTransfer
CH9344Driver::PrepareTx
CH9344Transport::StartTxLocked
NotifyTx -> TxDataAvailable
```

USB data OUT buffer 原以 `kIOMemoryDirectionOut` 创建，其地址在驱动中不可写；改为与 command buffer 一样使用 `kIOMemoryDirectionOutIn`，允许驱动先写入 TX 帧。build 3 已包含此修改；由于未执行新的发送测试，修复效果仍待实测。

崩溃报告：`/Library/Logs/DiagnosticReports/com.trekmax.OpenCH34xSer.driver-2026-11-04-204801.ips`。

### 4. 检测脚本使用了错误的名称和旧环境

当前 macOS 的 `IOUserSerial` 将 personality 中预设的 `OpenCH34x1..4` 替换为 `serial-<registry ID>`。旧脚本只检查 `/dev/*OpenCH34x*`，因此在真实四路节点存在时仍误报 0 个。

新增 `serial_nodes.py` 从 `CH9344Driver` 的 bundle ID、`CH9344LogicalPort` 和子级 `IOSerialBSDClient` 解析节点；验证节点确为字符设备，对多设备歧义报错。回环脚本默认按逻辑端口 4 查询，避免硬编码名称。activation 检查从实际应用签名读取 Team，并使用 `ioreg -n CH9344Driver`（其内核类不是 CH9344Driver）。Host/unsigned 测试移除已不存在的 Xcode-beta 路径，使用当前工具链。

## 签名环境与验证

- macOS 27.2（26B123），Xcode 27.1（27A9275）。
- 实际 TeamIdentifier 为 `9BT9NV52NL`；证书显示名 `Apple Development: TianShuang Ke (M6D66QWKAN)` 中的括号不是 TeamIdentifier。
- Host profile 有效至 2027-08-31；DEXT profile 有效至 2027-06-16。两者都登记了本机 `provisioning_UDID`，不能用不同的 `IOPlatformUUID` 代替。
- Host、DEXT 均包含 arm64 与 x86_64；本次实机仅验证 Apple Silicon。
- Debug 签名构建、实际 Host/DEXT 签名与 profile 检查通过。
- 默认 8 项 CTest 加 signed bundle / activation / serial nodes 共 **11/11 通过**；明确排除了 port 4 回环。
- 应用、安装位置及只读挂载 DMG 的 deep/strict/all-architectures 签名检查通过；DMG 校验通过。
- SIP 保持 enabled，原有 WCH 扩展未卸载或禁用。
- 保留用户原有暂存改动；未创建 Git 提交。

## 复现

```bash
xcodebuild \
  -project Driver/OpenCH34xSer.xcodeproj \
  -scheme OpenCH34xSerHost -configuration Debug \
  -destination 'generic/platform=macOS' \
  -derivedDataPath build/DriverKitDebugFix ONLY_ACTIVE_ARCH=NO build

python3 tests/driverkit/test_signed_bundle.py /Applications/OpenCH34xSerHost.app
bash tests/driverkit/test_activation.sh
python3 tests/driverkit/serial_nodes.py

cmake -S . -B build/verification-2026-11-04 \
  -DCH9344_ENABLE_HARDWARE_TESTS=OFF \
  -DCH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS=ON
cmake --build build/verification-2026-11-04
ctest --test-dir build/verification-2026-11-04 --output-on-failure \
  -E 'port4_tty_loopback'
```

第 4 路 TX/RX 物理短接、断开其他设备后，才运行 `python3 tests/driverkit/test_port4_tty_loopback.py`，覆盖 1/16/509 字节二进制回环。

## 产物与证据

- 当前 DMG：`build/packaging-2026-11-04/OpenCH34xSer-0.1.0-build3-development.dmg`。
- SHA-256：`c476a4a98513a2c71f2e3b7c2e13a5bc714ceb1862536b05fa7d675b8a207823`。
- 最初无 build 号的 `OpenCH34xSer-0.1.0-development.dmg` 是有问题的 build 1，仅作历史留存，不应继续安装。
- 构建与运行日志、旧版备份、拔插 registry、无数据打开检查：`build/debug-2026-11-04/`。
- 完整测试输出：`build/verification-2026-11-04/Testing/Temporary/LastTest.log`。

本包是 Apple Development / 已登记设备的开发安装包；未使用 Developer ID 签名、未公证，不作为公开分发包。
