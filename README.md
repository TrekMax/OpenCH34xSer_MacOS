# CH9344 macOS Driver

面向 WCH CH9344（USB VID `0x1a86`、PID `0xe018`）的 macOS 四路串口驱动验证与实现工程。

当前目标是在本机完成以下能力验证：

- 识别 CH9344 USB 设备及其四端口复用协议；
- 发布四个相互独立的标准串口服务；
- 支持波特率、8N1、RX、TX、DTR 和 RTS；
- 使用第 4 路 TX/RX 短接完成首个硬件回环验收。

项目采用测试驱动开发（TDD），所有功能遵循 RED、GREEN、REFACTOR 循环，并按阶段使用中文 Conventional Commits 提交。

## 当前状态

已经完成协议核心、端点分类和用户态 USB 验证工具。当前设备实测为 CH9344Q `0x42`，四个 Bulk 端点的最大包长均为 512。第 4 路已经通过 16 字节二进制载荷与 509 字节边界载荷回环。

评估、设计、实施计划和验证证据记录在 `docs/` 下。

## 本机构建与检查

依赖 CMake、支持 C++17 的 Apple Clang，以及能被 `pkg-config` 找到的 `libusb-1.0`。

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCH9344_ENABLE_HARDWARE_TESTS=ON
cmake --build build
ctest --test-dir build -L protocol --output-on-failure
ctest --test-dir build -R '^hardware_inspect$' --output-on-failure
build/ch9344-probe inspect
build/ch9344-probe loopback \
  --port 4 \
  --baud 115200 \
  --payload-hex 00017f80ff4348393334342d544444a5
build/ch9344-probe loopback --port 4 --baud 115200 --length 509
```

硬件测试默认关闭；只有连接目标 CH9344 时才启用 `CH9344_ENABLE_HARDWARE_TESTS`。
