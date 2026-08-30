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

## 阶段 3：宿主系统扩展生命周期

### RED

- 命令：`ctest --test-dir build -R '^host_system_extension_controller$' --output-on-failure`。
- 退出码：8。
- 预期失败：Swift 测试可编译、可运行，共 10 个行为断言失败；激活/停用请求均未提交，状态保持 `inactive`，版本替换策略始终返回 false。
- 判断：fake submitter、事件回调和测试 runner 正常，失败由宿主生命周期行为尚未实现造成。

### GREEN

- 命令：`ctest --test-dir build -R '^host_system_extension_controller$' --output-on-failure`，退出码 0。
- 结果：激活、停用、等待用户批准、成功、失败、重启后完成和拒绝降级全部通过。
- 原生 adapter：使用 `OSSystemExtensionRequest` 和 `OSSystemExtensionManager`，回调错误保留 NSError domain、code 与描述；相同开发 build 允许替换，低版本 build 被取消。
- 编译验证：Swift host 显式链接 SystemExtensions.framework，Xcode unsigned clean build 退出码 0、无警告。

## 阶段 4：单路 SerialDriverKit 配置入口

### RED

- 命令：`cmake --build build --target ch9344_driver_core_tests`。
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `buildUartConfiguration(...)` 与 `buildModemConfiguration(...)`。
- 判断：CH9344Q 第 4 路 115200/8N1、DTR/RTS、非法 line coding 和失败不写 output 测试均已编译，失败由配置 adapter 尚未实现造成。

### GREEN

- 命令：`cmake --build build --target ch9344_driver_core_tests && ctest --test-dir build -R '^driver_core$' --output-on-failure`，退出码 0。
- 结果：Q 型端口 4 的直接波特率字节、8N1 限制、DTR/RTS 与错误边界全部通过。
- DEXT adapter：`HwProgramUART`、`HwProgramBaudRate`、`HwProgramMCR` 已使用同一协议核心生成命令；在 USB transport 尚未接入时，有效命令返回 `kIOReturnNotReady`，不把“只编码未下发”伪报为成功。
- 编译验证：协议实现加入 DEXT target；Xcode unsigned clean build 退出码 0、静态分析无警告。为消除 analyzer 对已由长度保护的可空 TX payload 的误报，显式增加 `payload != nullptr` 的 memcpy 守卫，协议行为不变。

## 阶段 5：USBDriverKit transport 和芯片初始化

### RED 1：启动与回滚状态机

- 命令：`cmake --build build --target ch9344_usb_transaction_tests`。
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `initializeUsbTransport(...)` 与 `shutdownUsbTransport(...)`。
- 判断：fake backend 已锁定 `81/82/01/02` 获取顺序、Q `0x42`、12 条启动命令、4 次 status drain，以及 pipe/短版本/短写逆序回滚，失败由 orchestration 尚未实现造成。

### GREEN 1

- 命令：`cmake --build build --target ch9344_usb_transaction_tests && ctest --test-dir build -R '^usb_transaction$' --output-on-failure`，退出码 0。
- 结果：成功路径保持 transport 打开；全部失败路径按已获取 pipe 的逆序释放并关闭 interface。

### RED 2：运行时命令提交

- 命令：`cmake --build build --target ch9344_usb_transaction_tests`。
- 退出码：2。
- 预期失败：链接阶段找不到 `submitCommandSequence(...)`。
- 判断：两条 MCR 命令、精确长度、一次 drain 和第二条短写不 drain 测试已编译，失败由运行时提交行为尚未实现造成。

### GREEN 2 与 DriverKit adapter

- 命令：`ctest --test-dir build -R '^usb_transaction$' --output-on-failure`，退出码 0。
- DEXT `Start`：调用 super 后打开 `IOUSBHostInterface`，创建 512 字节 DriverKit I/O buffer，获取四个 pipe，以 control request `0xc0/0x96` 读取版本，并下发设备、端口 4、115200/8N1、DTR/RTS off 初始化；全部成功后才注册串口服务。
- DEXT 配置：UART、baud、MCR 命令完整写入 command OUT 且 status drain 成功后才返回 `kIOReturnSuccess`；短写或 USB 错误返回 `kIOReturnIOError`。
- 生命周期：启动失败和 `Stop` 逆序释放 pipe/buffer 并关闭 interface；`free` 只做兜底释放，不在 provider 已失效后关闭 interface。
- 编译验证：Xcode unsigned DriverKit build 退出码 0，静态分析无警告。首次 adapter 编译暴露 forward-declared owner 不能转换到 `IOService*`，将 state 的 owner 边界改为框架基类后通过。
