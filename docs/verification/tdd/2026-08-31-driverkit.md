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

## 阶段 6：单路 TX/RX 数据泵

### RED 1：USB 帧与 RX 背压

- 命令：`cmake --build build --target ch9344_driver_core_tests`。
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `prepareTxTransfer(...)` 与 `deliverRxTransfer(...)`。
- 判断：测试已覆盖第 4 路 TX ring 回绕、空 TX、RX 只接收 hardware port 7、RX ring 回绕、背压和畸形 transfer 不改 ring；失败由数据泵纯核心尚未实现造成。

### GREEN 1

- 命令：`cmake --build build --target ch9344_driver_core_tests && ctest --test-dir build -R '^driver_core$' --output-on-failure`，退出码 0。
- 结果：TX 以 512 字节 USB 上限生成最多 509 字节 payload 的 hardware port 7 帧，只返回候选 consumer index；RX 先完整解析并聚合目标端口数据，容量不足时整体背压，不产生部分写。

### RED 2：USB 完成提交规则

- 命令：`cmake --build build --target ch9344_driver_core_tests`。
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `completeTxTransfer(...)`。
- 判断：测试已锁定只有 active 且 USB 成功完整写入才提交 `txCI`；短写、USB 错误和停止后的 completion 都必须保留原 consumer index。

### GREEN 2 与 DriverKit adapter

- 单元命令：`ctest --test-dir build -R '^driver_core$' --output-on-failure`，退出码 0。
- 生命周期修正：依据 `IOUserSerial` 约定，`Start` 现在只保存 provider 并注册服务；USB interface 打开、芯片初始化和异步数据通道创建延迟到 `HwActivate`，`HwDeactivate` 同步取消未完成 I/O 后再释放 buffer/action 和关闭 interface。这取代了阶段 5 原型中在 `Start` 打开 USB 的临时实现。
- TX：`TxDataAvailable` 从映射后的 ring 组帧；只有 data OUT completion 完整成功才原子更新 `txCI`，然后通知 `TxFreeSpaceAvailable`。失败不提交且不无界重试。
- RX：data IN 保持单笔异步读取；hardware port 7 数据写入第 4 路 RX ring，空间不足时保留当前 transfer，等待 `RxFreeSpaceAvailable` 后重试；畸形记录报告 framing error 并继续下一笔读取。
- 队列与停止安全：`ConnectQueues` 校验映射、偏移与 ring 大小；断开或停用先同步 abort，再释放映射/缓冲，completion 在 inactive 状态不提交索引。
- 编译命令：`DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild -quiet -project Driver/OpenCH34xSer.xcodeproj -scheme OpenCH34xSerHost -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath build/DriverKit CODE_SIGNING_ALLOWED=NO clean build`，退出码 0，无编译和静态分析警告。
- 完整验证：CMake 构建与 8/8 CTest 通过，包含协议、DriverKit、宿主生命周期以及真实 CH9344 inspect/第 4 路 libusb 回环；该硬件回环证明协议未回归，不等同于尚未激活的 `/dev/cu.*` 节点验收。

## 阶段 7：签名、激活与标准串口节点

### GREEN：unsigned bundle 验收

- 命令：`bash tests/driverkit/test_unsigned_bundle.sh`，退出码 0。
- 结果：脚本在临时 DerivedData 中执行无签名 clean build，确认 Host 内嵌 DEXT、bundle identifier、`0x1a86:0xe018` personality、DriverKit/serial/USB/System Extension entitlement 源配置，并以 `otool -L` 确认 DEXT 只使用原生 DriverKit 相关框架且不链接 libusb。

### RED：签名 provisioning

- 身份检查：`security find-identity -v -p codesigning` 找到 1 个有效身份 `Apple Development: TianShuang Ke (M6D66QWKAN)`。
- 现有 profile：本机只有 `iOS Team Provisioning Profile: *`，Team `9BT9NV52NL`，不属于工程 Team，且不含 DriverKit entitlement。
- 不允许自动更新的命令：`xcodebuild ... -derivedDataPath build/DriverKitSigned clean build`，退出码 65；Host 缺少 Mac App Development profile，DEXT 缺少 DriverKit App Development profile。
- 允许自动更新的命令：`xcodebuild ... -derivedDataPath build/DriverKitSigned -allowProvisioningUpdates clean build`，退出码 65；Xcode 报 `No Accounts: Add a new account in Accounts settings`，并记录旧 keychain credential 缺少 `Xcode-Username`。
- 判断：代码签名身份存在，但 Xcode 没有可用的 Team 账号，无法创建/下载 Host 与 DriverKit profile。SIP 保持 enabled；未关闭 SIP、未开启未授权的安全绕过，也未把 ad-hoc 签名当成 DriverKit entitlement。

### RED：激活与节点

- 激活命令：`bash tests/driverkit/test_activation.sh`，退出码 1，结果为 `com.trekmax.OpenCH34xSer.driver is not registered`。
- 节点命令：`bash tests/driverkit/test_serial_nodes.sh 1`，退出码 1，结果为期望 1 对、实际 0 对 OpenCH34x 标准串口节点。
- 当前系统只激活 WCH `cn.wch.CH34xVCPDriver`；该扩展不匹配 `0xe018`，所以仍不会产生 CH9344 节点。
- 系统验收脚本仅在 `CH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS=ON` 时注册，避免把外部 provisioning 状态混入默认单元/unsigned 回归；获得 profile 并激活后必须显式开启，未通过前本阶段保持阻塞而非 GREEN。

## 阶段 8：共享 transport 与四路串口服务

### RED 1：四路调度与 RX 路由状态

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON`。
- 退出码：1。
- 预期失败：CMake 找不到 `Driver/Shared/PortScheduler.cpp`，新增的四路轮转、无效 mask、游标归一化、交错 RX port mask 和畸形 RX 测试无法生成目标。
- 判断：失败由四路 scheduler 尚未实现造成，不是测试环境错误。

### GREEN 1

- 命令：`cmake --build build --target ch9344_port_scheduler_tests && ctest --test-dir build -R '^port_scheduler$' --output-on-failure`，退出码 0。
- 结果：ready port 按游标轮转，繁忙端口不能永久饿死其他端口；RX transfer 先完整校验并一次性生成逻辑端口 mask，后续可逐端口清除已交付位，支持某一路背压时不重复投递其他路。

### RED 2：共享 USB 一次初始化四路

- 命令：`cmake --build build --target ch9344_usb_transaction_tests`。
- 退出码：2。
- 预期失败：arm64 链接阶段找不到 `initializeAllPortsUsbTransport(...)`。
- 判断：测试已要求只打开一次 interface、只获取一组四 endpoint、只执行一次 device initialization，并初始化逻辑端口 0～3。

### GREEN 2

- 命令：`ctest --test-dir build -R '^usb_transaction$' --output-on-failure`，退出码 0。
- 结果：当前 Q 芯片共享启动产生 45 条精确命令和 13 次 status drain；端口寄存器基址依次为 `0x0a/0x1a/0x2a/0x3a`，失败仍统一逆序回滚 pipe 并关闭 interface。

### RED 3：DriverKit 四子服务元数据

- 命令：`bash tests/driverkit/test_project_metadata.sh`。
- 退出码：1。
- 预期失败：缺少 `Driver/Extension/CH9344Transport.iig`。
- 判断：元数据测试已从单路 prototype 更新为一个 `IOUserService` USB transport 与四个 `IOUserSerial` 子服务，要求 suffix 1～4 和逻辑端口 0～3 一一对应。

### GREEN 3 与 adapter

- transport：`CH9344Transport` 是唯一 `IOUSBHostInterface` owner，集中持有四个 pipe、命令锁、data IN/OUT action 与轮转游标；通过 `IOService::Create()` 从 personality 的四份属性创建 `CH9344Driver` 子服务，任一创建/注册失败会逆序终止全部已创建端口。
- serial port：四个 `CH9344Driver` 各自映射独立 SerialDriverKit ring，并把 UART/DTR/RTS、TX 通知和 RX 空间通知委托给 transport。data OUT completion 只提交对应端口候选 `txCI`；RX 逐端口交付并保留背压 mask。
- 生命周期：Stop 先令 transport inactive 并终止四个子服务，再同步 abort 数据 I/O、释放 action/buffer、逆序释放 pipe 并关闭 USB interface；停止后的 completion 不推进任何端口索引。
- 编译命令：`DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild -quiet -project Driver/OpenCH34xSer.xcodeproj -scheme OpenCH34xSerHost -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath build/DriverKit CODE_SIGNING_ALLOWED=NO clean build`，退出码 0，无编译和静态分析警告。
- 完整回归：10/10 CTest 通过，包含新增 scheduler、四路共享 USB 初始化、四子服务 metadata、unsigned bundle、宿主生命周期以及真实硬件 inspect/第 4 路 libusb 回环。
- 系统边界：`test_serial_nodes.sh` 的正式期望已更新为恰好 4 对节点，但因阶段 7 provisioning 阻塞尚不能运行到 GREEN；第 1～3 路未短接，只能在激活后验收节点、配置和路由隔离，不能宣称物理回环。
