# 构建、签名安装与验证

[返回 README](../README.md)

以下命令均在仓库根目录执行。构建需要 Xcode、CMake 3.25 及以上、pkg-config 和 libusb；libusb 用于构建诊断 probe，不是最终 App/DEXT 的运行时依赖。

## 名称与签名身份

应用名称为 **OpenCH93xxSer**，Xcode 工程和共享 scheme 同名，安装路径为 `/Applications/OpenCH93xxSer.app`。

为沿用现有 Apple provisioning profile、原位升级已授权驱动，内部 Bundle ID 保留 `com.trekmax.OpenCH34xSer` 和 `com.trekmax.OpenCH34xSer.driver`；对应 DEXT 文件名也保留。系统显示名称已改为 `OpenCH93xxSer Driver`。历史验证记录中的旧应用名称代表当时实际产物。

## 默认构建与测试

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCH9344_ENABLE_HARDWARE_TESTS=OFF
cmake --build build
ctest --test-dir build --output-on-failure

xcodebuild -quiet \
  -project Driver/OpenCH93xxSer.xcodeproj \
  -scheme OpenCH93xxSer \
  -configuration Debug \
  -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath build/DriverKit \
  CODE_SIGNING_ALLOWED=NO \
  clean build
```

默认测试不操作设备。早期 libusb 测试通过 `CH9344_ENABLE_HARDWARE_TESTS=ON` 启用，会独占 CH9344；不要在 DEXT 已绑定设备时运行。构建脚本使用 `xcode-select` 当前 Xcode，也可通过 `DEVELOPER_DIR` 覆盖。

## 签名、安装与最终验收

1. 在 Xcode 的 Settings > Accounts 登录 Team `9BT9NV52NL` 对应账号。
2. 确认该 Team 已获 DriverKit、DriverKit Serial Family 和 DriverKit USB Transport entitlement，并能为两个 bundle identifier 生成 profile：
   - `com.trekmax.OpenCH34xSer`
   - `com.trekmax.OpenCH34xSer.driver`
3. 运行允许 Xcode 获取 profile 的签名构建：

```bash
xcodebuild \
  -project Driver/OpenCH93xxSer.xcodeproj \
  -scheme OpenCH93xxSer \
  -configuration Debug \
  -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath build/DriverKitSigned \
  -allowProvisioningUpdates \
  clean build
```

4. 将 `build/DriverKitSigned/Build/Products/Debug/OpenCH93xxSer.app` 复制到 `/Applications` 后打开，点击“激活驱动”，并按界面提示在系统设置批准 Driver Extension；若提示重启则先重启。
5. 重新插拔 CH9344，再运行系统验收：

```bash
cmake -S . -B build-system \
  -DCH9344_ENABLE_HARDWARE_TESTS=OFF \
  -DCH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS=ON
ctest --test-dir build-system -R '^driverkit_' -E 'port4_tty_loopback' --output-on-failure
```

系统测试核对扩展 active/enabled、实际签名/profile 一致，以及四对属于本驱动的串口节点。上述命令排除发送数据的回环测试。第 4 路 TX/RX 短接并断开其他设备后，可运行：

```bash
python3 tests/driverkit/test_port4_tty_loopback.py
```

Debug 使用 `CH9344Driver.development.entitlements`，USB entitlement 与开发 profile 一致为 `[{idVendor: "*"}]`，设备匹配仍限制为 `1a86:e018`。Release 保留具体 VID/PID 的 entitlement，需要匹配的发布 profile；本次未制作 Developer ID / 公证发布包。

不要为了本地调试关闭 SIP 或绕过 DriverKit entitlement。若签名仍失败，应保留 Xcode 原始错误并修复账号/profile，而不是将 ad-hoc bundle 当成可安装驱动。

## 当前验证状态

2026-11-05：更名后的 **OpenCH93xxSer 0.1.0 build 8** 已签名、安装并由真实设备接管。新图标已在关于窗口核对；第 4 路通过 23 项常规回环、12 Mbps / 1 MiB 回环（0.951 秒）及随后 10 次重开收发。安装包为 `build/packaging-2026-11-05/OpenCH93xxSer-0.1.0-build8-development.dmg`。名称变量、签名身份保留、升级过程及待验证事项见 [更名与图标验收](verification/2026-11-05-opench93xx-branding.md)。

此前 build 7 已实测 Apple Development 签名的双架构 Debug 构建。第 4 路标准串口回环通过 9600/115200 的边界长度、115200 的 64 KiB、921600 和 12000000（12 Mbps）的 1 MiB 随机连续数据，以及高速测试后各 10 次重开收发。12 Mbps 配置下 1 MiB 回环耗时 0.951 秒。原 9600/4096 字节内容错误和跨 16 KiB 队列后停发均通过原失败用例复测。详见 [回环记录](verification/2026-11-05-dext-loopback.md)。

四路实际名称已改为 `serialCH93xxUSB0-<ID>`～`serialCH93xxUSB3-<ID>`（tty/cu 成对），并通过实机核对。打开但无数据、少量发送后、用户原程序运行后、build 6 回环后拔插均清除旧节点；原 build 3 残留的根因仍未确定。曾有 Start 失败的旧驱动进程阻塞升级，重启后已清除，此失败路径仍待专门验证。详见 [拔插与命名记录](verification/2026-11-05-hotplug-naming.md)。

主机 CTest 8/8、环形队列 ASan/UBSan 测试、应用签名/profile、扩展激活和节点归属检查通过。四路枚举和命名均已验证；实际 TX/RX 回环仅覆盖第 4 路，其他三路回环、长时间稳定性与 build 7 重启恢复尚未验证。

此前的签名权限、USB configuration 选择及 TX 只读缓冲区崩溃排查见 [签名安装记录](verification/2026-11-04-signed-install.md)。

当前节点名含逻辑端口 0～3，末尾 registry ID 随重新枚举变化。连接设备并激活驱动后，使用以下命令查询实时映射；没有节点时，脚本给出连接提示并以状态码 1 退出：

```bash
python3 tests/driverkit/serial_nodes.py
python3 tests/driverkit/serial_nodes.py --port 4
```

完整的阶段设计与实施计划保留在 `docs/superpowers/`，历史验证记录保留在 `docs/verification/`；记录中的路径均相对于仓库根目录。
