# OpenCH93xxSer 更名与 App 图标验收

## 变更

- Xcode 工程：`Driver/OpenCH93xxSer.xcodeproj`；共享 scheme/Host target：`OpenCH93xxSer`。
- 应用：`OpenCH93xxSer.app`，0.1.0 build 8；Swift 入口 `OpenCH93xxSerApp.swift`。
- App 的 `CFBundleName`、`CFBundleDisplayName` 均使用 `$(PRODUCT_NAME)`；窗口标题和页面标题读取 App 元数据。DEXT 用 `$(DRIVER_DISPLAY_NAME)` 显示 `OpenCH93xxSer Driver`。
- 两个已有 Bundle ID 和 DEXT 文件名保留 `com.trekmax.OpenCH34xSer` / `com.trekmax.OpenCH34xSer.driver`，与现有开发 profile 匹配，并沿用已授权驱动身份。
- 新增 1024 PNG 与包含 10 个标准尺寸表示的 ICNS。图标已加入 Host Resources，`CFBundleIconFile=AppIcon`。生成方式、完整提示词和封装命令见 [图标说明](../../Driver/Host/Resources/README.md)。
- 已有暂存区二进制 patch 与 `build/packaging-2026-11-04/staged-before.patch` 完全一致；重命名仅作用于工作区，未提交或推送。

## 构建与安装

```bash
xcodebuild -project Driver/OpenCH93xxSer.xcodeproj \
  -scheme OpenCH93xxSer -configuration Debug \
  -destination 'generic/platform=macOS' \
  -derivedDataPath build/OpenCH93xxSerDebug ONLY_ACTIVE_ARCH=NO build
python3 tests/driverkit/test_signed_bundle.py /Applications/OpenCH93xxSer.app
```

Apple Development 签名构建成功；App 和 DEXT 均包含 arm64 / x86_64。主机 CTest 8/8 通过；最后一次名称变量调整后重新构建并复测 metadata/unsigned bundle 2/2 通过。实际包的名称、版本、图标资源、签名、受限 entitlement 与 profile 均通过核对。

旧 App 备份至 `build/branding-2026-11-05/OpenCH34xSerHost-build7.app`，新 App 已安装到 `/Applications/OpenCH93xxSer.app`。实际 GUI 的窗口、菜单和关于窗口均显示 OpenCH93xxSer；关于窗口可见新图标和 `Version 0.1.0 (8)`。

## 实机

激活请求后，build 7 曾处于 `terminating for upgrade via delegate`。用户重新插拔后，系统仅保留 build 8 的 activated/enabled 注册，实际进程来自：

```text
/Library/SystemExtensions/479D8088-F0F8-4A82-810E-3709047E5C1A/com.trekmax.OpenCH34xSer.driver.dext
```

四路当前后缀为 `100001D23 / 100001D24 / 100001D26 / 100001D28`；旧后缀 `100001B23 / 100001B24 / 100001B26 / 100001B28` 对应的八个节点均已删除。`test_serial_lifecycle.py attached --previous ...` 通过。

第 4 路 `/dev/cu.serialCH93xxUSB3-100001D28` TX/RX 短接验收：

- 9600、115200 各 11 个边界长度，以及 115200 / 65536 字节：23/23 通过。
- 12000000 baud：1、30、31、509、510、4096、65536、1048576 字节随机回环全部逐字节一致；1 MiB 用时 0.951 秒。
- 12 Mbps 测试后，115200 / 509 字节重开收发 10 次通过，节点集合未改变。

日志与 JSON：`build/branding-2026-11-05/build8-loopback.log`、`build8-12mbps.log`、`build8-12mbps-speed-results.json`。这验证的是配置与收发结果，未使用示波器测量物理位宽。

升级尚未结束时的一次重复激活返回 `OSSystemExtensionErrorDomain code=4`（ExtensionNotFound）；拔插完成后再次激活成功，界面恢复“驱动已激活”。当时系统实际已注册 build 8，不能把该错误解释为签名失败。记录保留在 `sysext-upgrade.log`。

仍观察到一个较早枚举关联的 build 8 进程（PID 9954 / registry ID `0x100001ced`），此外为当前设备和接口进程（9973 / 9974）；没有额外串口节点。此前 Start 失败路径的进程清理问题继续保留待验证，未因本次更名宣称修复。其他三路实际回环、四路同时满载、长时间稳定性及 build 8 重启恢复仍待验证。

## 安装包

`build/packaging-2026-11-05/OpenCH93xxSer-0.1.0-build8-development.dmg`

SHA-256：`00212cf3702f2e8cec2f9c32d48e4705f3d0d2bd11e81378fac80aed9269b0ad`

DMG 校验、只读挂载和包内 App/DEXT 签名检查通过；包内程序、元数据及图标与实际安装版本逐字节一致，随后已卸载镜像。包内提供 Applications 链接、安装说明和独立 PNG/ICNS 图标。此包为开发设备使用的 Apple Development / Debug 构建，未制作 Developer ID 公证发行版。
