# OpenCH34xSer macOS Driver

面向 WCH CH9344（USB VID `0x1a86`、PID `0xe018`）的原生 macOS 四路串口 DriverKit 工程。

工程包含：

- Swift 宿主 App：使用 SystemExtensions 框架激活、升级和停用 DEXT；
- C++17 DriverKit DEXT：独占 CH9344 interface 0，使用 USBDriverKit 和 SerialDriverKit 发布四路标准串口；
- C++17 共享核心：CH9344 协议、端点识别、环形队列、四路 RX 路由、TX 公平调度和 USB 启停状态机；
- libusb probe：只用于驱动激活前的协议和硬件验证，不是最终驱动的运行时依赖。

## 为什么诊断命令叫 lsusb

`cyme --lsusb` 的名称和输出格式沿用了 Linux `lsusb` 的使用习惯，但当前 macOS 版本的 cyme 直接链接 Apple IOKit 与 CoreFoundation。它调用的是 macOS 原生 USB 枚举能力，不需要 Linux USB 子系统，也不代表驱动必须使用 libusb。

本工程里的 libusb probe 同样只承担早期用户态协议验证。最终 Host 和 DEXT 的动态依赖中没有 libusb，USB I/O 全部使用 USBDriverKit。

## 为什么 DEXT 使用 C++

DriverKit 的驱动类通过 IIG 接口定义并以 C++ 实现；USBDriverKit 和 SerialDriverKit 的驱动侧 API 也是 C++ API。因此 DEXT 采用 C++17 是平台原生、接口最直接且最容易审计的选择。

Swift 适合宿主 App 和 `OSSystemExtensionRequest` 生命周期，本工程已经这样使用。Rust 没有 Apple 官方 DriverKit/IIG 绑定；可以把纯协议核心封装为 Rust 静态库，但仍需 C++ adapter，并会额外引入 ABI、panic、分配器和 DriverKit 运行时验证成本，所以当前范围不采用。

## 驱动架构

```text
CH9344 USB interface 0
        |
        v
CH9344Transport (唯一 USB owner)
        |
        +-- CH9344Driver port 1 -> /dev/cu.OpenCH34x1
        +-- CH9344Driver port 2 -> /dev/cu.OpenCH34x2
        +-- CH9344Driver port 3 -> /dev/cu.OpenCH34x3
        +-- CH9344Driver port 4 -> /dev/cu.OpenCH34x4
```

四路共享 command IN `0x81`、data IN `0x82`、command OUT `0x01`、data OUT `0x02`。RX 32 字节记录按 hardware port 4～7 解复用；TX 以最多 509 字节 payload 组帧并轮转调度。当前支持 baud、8N1、RX、TX、DTR 和 RTS，不包含 CH348、GPIO、RS-485、9-bit 或硬件流控。

## 当前验证状态

已完成并通过：

- 当前硬件识别为 CH9344Q，version `0x42`；
- 协议、端点、ring、四路调度、USB 回滚和 Host 生命周期单元测试；
- 第 4 路 libusb 硬件回环：9600、115200、921600，以及 1/16/509 字节边界；
- Host + DEXT 无签名双架构 clean build和静态分析；
- unsigned bundle 布局、四路 personality、原生框架依赖与“无 libusb”检查；
- 默认完整矩阵 10/10 CTest。

尚未完成：签名 DEXT 激活、四对 `/dev/cu.*`/`/dev/tty.*` 节点、第 4 路标准 TTY 回环和重插拔。当前本机 Xcode 没有可用开发者账号，也没有 Team `M6D66QWKAN` 的 Mac App Development/DriverKit App Development profile。无签名构建成功不等同于驱动已安装。

## 默认构建与测试

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCH9344_ENABLE_HARDWARE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
  xcodebuild -quiet \
  -project Driver/OpenCH34xSer.xcodeproj \
  -scheme OpenCH34xSerHost \
  -configuration Debug \
  -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath build/DriverKit \
  CODE_SIGNING_ALLOWED=NO \
  clean build
```

硬件测试会独占当前 CH9344，不应并行运行。没有连接设备时，将 `CH9344_ENABLE_HARDWARE_TESTS` 设为 `OFF`。

## 签名、安装与最终验收

1. 在 Xcode 的 Settings > Accounts 登录 Team `M6D66QWKAN` 对应账号。
2. 确认该 Team 已获 DriverKit、DriverKit Serial Family 和 DriverKit USB Transport entitlement，并能为两个 bundle identifier 生成 profile：
   - `com.trekmax.OpenCH34xSer`
   - `com.trekmax.OpenCH34xSer.driver`
3. 运行允许 Xcode 获取 profile 的签名构建：

```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
  xcodebuild \
  -project Driver/OpenCH34xSer.xcodeproj \
  -scheme OpenCH34xSerHost \
  -configuration Debug \
  -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath build/DriverKitSigned \
  -allowProvisioningUpdates \
  clean build
```

4. 打开 `build/DriverKitSigned/Build/Products/Debug/OpenCH34xSerHost.app`，点击“激活驱动”，并按界面提示在系统设置批准 Driver Extension；若提示重启则先重启。
5. 重新插拔 CH9344，再运行系统验收：

```bash
cmake -S . -B build-system \
  -DCH9344_ENABLE_HARDWARE_TESTS=OFF \
  -DCH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS=ON
ctest --test-dir build-system -R '^driverkit_' --output-on-failure
```

系统测试要求扩展处于 active/enabled、IORegistry 已绑定、恰好出现四对 OpenCH34x 节点，并通过 `/dev/cu.OpenCH34x4` 的 1/16/509 字节二进制回环。第 1～3 路当前没有物理短接，因此只验收节点、配置和路由隔离。

不要为了本地调试关闭 SIP 或绕过 DriverKit entitlement。若签名仍失败，应保留 Xcode 原始错误并修复账号/profile，而不是将 ad-hoc bundle 当成可安装驱动。

详细设计、阶段计划和 RED/GREEN 证据见 `docs/`，当前交付边界见 `docs/verification/2026-08-31-final-driver.md`。
