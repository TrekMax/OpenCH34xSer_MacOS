# CH9344 DriverKit TDD 记录

日期：2026-08-31

## 阶段 1：SerialDriverKit 环形队列核心

### RED

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON && cmake --build build --target ch9344_driver_core_tests`
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `ch9344::driver::peekTxRing(...)` 与 `ch9344::driver::writeRxRing(...)`。
- 判断：连续、回绕、空/满、部分复制与非法 metadata 测试均已编译，失败由目标环形队列行为尚未实现造成。

### GREEN

- 命令：`cmake --build build --target ch9344_driver_core_tests && ctest --test-dir build -R '^driver_core$' --output-on-failure && cmake --build build && ctest --test-dir build --output-on-failure`
- 退出码：0。
- 结果：新增 `driver_core` 通过；完整 5/5 CTest 通过，其中协议 2 项、DriverKit 核心 1 项、真实 CH9344 inspect/第 4 路回环 2 项。
- 实现边界：helper 只计算并返回 next index；TX consumer 必须由未来 USB 完整发送 completion 提交，peek 本身不修改 SerialDriverKit 索引。RX 保留一个空槽区分满与空，任何非法 metadata 都不会复制数据。

## 阶段 2：原生宿主 App 与 DEXT 工程骨架

### RED

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON && ctest --test-dir build -R '^driverkit_project_metadata$' --output-on-failure`
- 退出码：8。
- 预期失败：`test_project_metadata.sh` 报告缺少 `Driver/OpenCH34xSer.xcodeproj/project.pbxproj`。
- 判断：测试脚本已正常启动并检查第一个原生工程产物，失败由 host/DEXT 工程尚未创建造成。

### GREEN

- metadata 命令：`ctest --test-dir build -R '^driverkit_project_metadata$' --output-on-failure`，退出码 0。
- 编译命令：`DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild -quiet -project Driver/OpenCH34xSer.xcodeproj -scheme OpenCH34xSerHost -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath build/DriverKit CODE_SIGNING_ALLOWED=NO clean build`，退出码 0，无编译和静态分析警告。
- 完整验证：CMake 构建与 6/6 CTest 通过，包含 2 项协议、2 项 DriverKit、2 项真实硬件测试。
- bundle 结果：DEXT 位于 `OpenCH34xSerHost.app/Contents/Library/SystemExtensions/com.trekmax.OpenCH34xSer.driver.dext`；Mach-O 同时包含 arm64/x86_64，动态依赖仅为 DriverKit、USBDriverKit、SerialDriverKit 和 DriverKit libc++，不依赖 libusb。
- 调试修正：首次 Xcode 静态分析指出允许空 output/input 的零长度分支仍可能到达 `memcpy`；收紧 ring helper 指针契约后 clean build 警告归零，原有单元测试保持绿色。
