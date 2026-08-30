# CH9344 DriverKit 最终驱动实施计划

**目标：** 在当前 Apple Silicon Mac 上交付一个可安装的原生 macOS 驱动：匹配 CH9344 `0x1a86:0xe018`，发布四路标准串口，并通过第 4 路真实 TX/RX 回环。

**架构：** Swift 宿主 App 负责 System Extension 的激活、升级和停用；C++ DriverKit DEXT 以 `IOUSBHostInterface` 为 provider，独占 interface 0 的四个 Bulk endpoint。共享 USB transport 使用 `IOService::Create()` 创建四个 `IOUserSerial` 子服务。平台无关的协议、环形队列和四路调度逻辑继续使用 C++17 单元测试；DriverKit adapter 通过编译测试与当前机器系统测试验收。

**技术栈：** Swift、C++17、DriverKit、USBDriverKit、SerialDriverKit、SystemExtensions、Xcode 27 beta、CMake/CTest、Apple Clang、zsh。

## 全局约束

- 最终驱动只匹配 VID `0x1a86`、PID `0xe018`、configuration 1、interface 0。
- 只支持四路 UART、baud、8N1、RX、TX、DTR、RTS；不扩展 CH348、GPIO、RS-485、9-bit 或硬件流控。
- `libusb` 只属于 probe；宿主 App 和 DEXT 不链接、不加载 `libusb`。
- 第 4 路是当前唯一 TX/RX 已短接端口。第 1～3 路只能验收节点、配置、USB 路由和隔离，不能宣称物理 RX 已通过。
- 每个生产行为执行严格 RED → GREEN → REFACTOR → VERIFY；RED 记录到验证文档，不提交已知失败 revision。
- 构建系统和签名配置以“缺少目标/元数据时测试失败，补齐后通过”的可执行检查驱动；不为没有行为的 plist 文本伪造单元测试。
- 每个阶段独立中文 Conventional Commit，只暂存明确路径，不 push。
- DEXT 激活所需的 Apple DriverKit entitlement/provisioning profile 是外部前置条件；先完成无签名编译和 bundle 检查，再以实际签名日志判定，不预先假设已获授权。

## 目标文件结构

```text
Driver/
  OpenCH34xSer.xcodeproj/
  Host/
    OpenCH34xSerApp.swift
    SystemExtensionController.swift
    OpenCH34xSerHost.entitlements
    Info.plist
  Extension/
    CH9344Driver.iig
    CH9344Driver.cpp
    CH9344Driver.entitlements
    Info.plist
  Shared/
    DriverCore.hpp
    DriverCore.cpp
tests/
  driverkit/
    DriverCoreTests.cpp
    test_project_metadata.sh
    test_unsigned_bundle.sh
    test_activation.sh
    test_serial_nodes.sh
    test_port4_tty_loopback.py
    test_replug.sh
docs/verification/tdd/2026-08-31-driverkit.md
docs/verification/2026-08-31-final-driver.md
```

## 固定验证命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
  xcodebuild -project Driver/OpenCH34xSer.xcodeproj \
  -scheme OpenCH34xSerHost -configuration Debug \
  -destination 'platform=macOS' CODE_SIGNING_ALLOWED=NO build

git diff --check
```

涉及真实 CH9344 的测试必须串行执行，所有 CTest 硬件项继续使用 `RESOURCE_LOCK ch9344`。

---

## 阶段 1：驱动核心环形队列

**文件：**

- 新建 `Driver/Shared/DriverCore.hpp`
- 新建 `Driver/Shared/DriverCore.cpp`
- 新建 `tests/driverkit/DriverCoreTests.cpp`
- 修改 `CMakeLists.txt`
- 新建 `docs/verification/tdd/2026-08-31-driverkit.md`

**行为：**

- 从 SerialDriverKit TX ring 的 `txCI` 消费不超过指定上限的数据，正确处理尾部回绕，仅在成功交付后推进 consumer index。
- 向 RX ring 的 `rxPI` 写入不超过空闲容量的数据，正确处理回绕，不覆盖尚未消费的数据。
- ring 大小必须是 `1 << logSize`；producer/consumer 超界或无效 logSize 返回错误且不修改索引。

**TDD：**

1. 先声明 `readTxRing`、`writeRxRing`，用手工字节 fixture 覆盖连续、回绕、空、满、部分写、非法索引。
2. 运行测试并观察 undefined symbol RED。
3. 写最小的两段式 copy 和索引更新实现。
4. 跑全部 CTest、`git diff --check`。
5. 提交：`feat: 实现 DriverKit 串口环形队列核心`。

## 阶段 2：原生宿主 App 与 DEXT 工程骨架

**文件：**

- 新建 `Driver/OpenCH34xSer.xcodeproj/project.pbxproj`
- 新建 `Driver/OpenCH34xSer.xcodeproj/xcshareddata/xcschemes/OpenCH34xSerHost.xcscheme`
- 新建 `Driver/Host/OpenCH34xSerApp.swift`
- 新建 `Driver/Host/Info.plist`
- 新建 `Driver/Host/OpenCH34xSerHost.entitlements`
- 新建 `Driver/Extension/CH9344Driver.iig`
- 新建 `Driver/Extension/CH9344Driver.cpp`
- 新建 `Driver/Extension/Info.plist`
- 新建 `Driver/Extension/CH9344Driver.entitlements`
- 新建 `tests/driverkit/test_project_metadata.sh`

**可执行验收：**

- 宿主 product type 是 macOS application；DEXT product type 是 driver extension。
- DEXT 嵌入宿主的 `Contents/Library/SystemExtensions`。
- 宿主具备 `com.apple.developer.system-extension.install`。
- DEXT 具备 DriverKit、USB transport 与 serial family entitlement。
- USB personality 精确包含十进制 `idVendor=6790`、`idProduct=57368`、configuration 1、interface 0、`IOProviderClass=IOUSBHostInterface`。
- 初始 DEXT 类继承 `IOUserSerial`，只发布逻辑第 4 路以完成单路原型；`IOClass=IOUserSerial`。

**TDD：**

1. 先写 `test_project_metadata.sh` 检查上述 project/plist/entitlement/bundle 关系，运行并因文件缺失 RED。
2. 依据本机 Xcode DriverKit 模板创建最小 Swift host 和 C++/IIG DEXT。
3. 运行 metadata GREEN。
4. 以 `CODE_SIGNING_ALLOWED=NO` 编译，修复到 host 和 DEXT 都无编译/链接错误。
5. 全量验证后提交：`build: 创建 CH9344 DriverKit 原生工程`。

## 阶段 3：宿主系统扩展生命周期

**文件：**

- 修改 `Driver/Host/OpenCH34xSerApp.swift`
- 修改 `Driver/Host/SystemExtensionController.swift`
- 新建宿主单元测试 target 与 `Driver/HostTests/SystemExtensionControllerTests.swift`

**行为：**

- 用户可明确触发激活和停用；界面展示提交中、等待批准、已激活、失败及错误码。
- 激活 bundle identifier 固定为 DEXT bundle identifier。
- replacement policy 只允许当前版本更新旧版本，不允许降级。
- 宿主退出不会自动停用驱动。

**TDD：**

1. 为 request factory 和 manager 建立小协议，用 fake 验证激活/停用请求、identifier、状态迁移和错误传播；先观察类型/行为缺失 RED。
2. 写最小 `OSSystemExtensionRequestDelegate` adapter 达到 GREEN。
3. 无签名构建和宿主单元测试通过。
4. 提交：`feat: 实现 CH9344 驱动安装管理`。

## 阶段 4：单路 SerialDriverKit 配置入口

**文件：**

- 修改 `Driver/Extension/CH9344Driver.iig`
- 修改 `Driver/Extension/CH9344Driver.cpp`
- 修改 `Driver/Shared/DriverCore.hpp`
- 修改 `Driver/Shared/DriverCore.cpp`
- 修改 `tests/driverkit/DriverCoreTests.cpp`

**行为：**

- 实现 `IOUserSerial` 全部必需抽象方法。
- `HwProgramUART` 只接受 5～8 data bits、合法 stop/parity；首期系统验收要求 8N1。
- `HwProgramBaudRate` 复用已验证协议编码。
- `HwProgramMCR` 生成 DTR/RTS 命令。
- 不支持的 flow control 返回明确错误；latency timer 有界保存；reset/break 不伪报已执行。

**TDD：**

1. 在纯 `DriverCoreTests` 先写 termios 参数到协议命令的测试，覆盖 9600、115200、921600、非法配置、DTR/RTS。
2. 观察新增 API undefined RED，再实现最小 adapter。
3. DEXT 仅把 DriverKit 返回码和状态映射到该纯核心；以无签名 xcodebuild 作为 adapter 编译测试。
4. 提交：`feat: 实现 CH9344 单路串口配置入口`。

## 阶段 5：USBDriverKit transport 和芯片初始化

**文件：**

- 修改 `Driver/Extension/CH9344Driver.iig`
- 修改 `Driver/Extension/CH9344Driver.cpp`
- 新建 `Driver/Shared/UsbTransaction.hpp`
- 新建 `Driver/Shared/UsbTransaction.cpp`
- 新建 `tests/driverkit/UsbTransactionTests.cpp`
- 修改 `CMakeLists.txt`

**行为：**

- `Start` 调用 super，强制转换并打开 `IOUSBHostInterface`。
- 精确取得 command IN `0x81`、data IN `0x82`、command OUT `0x01`、data OUT `0x02` 四个 pipe；缺一即完整回滚。
- 读取版本并只接受已验证的 CH9344L/Q；当前设备必须识别为 CH9344Q `0x42`。
- 初始化成功后配置第 4 路并 `RegisterService()`；任一步失败都不发布节点。
- `Stop` 取消未完成 I/O、关闭 interface、释放 pipe/buffer/action。

**TDD：**

1. fake USB transaction 测试先覆盖成功顺序、短传输、错误、回滚逆序和“失败不注册”。
2. 观察 orchestration API 缺失 RED，写最小状态机到 GREEN。
3. DriverKit adapter 编译通过；probe 硬件测试仍全绿，证明协议行为未回归。
4. 提交：`feat: 实现 CH9344 DriverKit USB 传输`。

## 阶段 6：单路 TX/RX 数据泵

**文件：**

- 修改 `Driver/Extension/CH9344Driver.iig`
- 修改 `Driver/Extension/CH9344Driver.cpp`
- 修改 `Driver/Shared/DriverCore.*`
- 修改 `tests/driverkit/DriverCoreTests.cpp`

**行为：**

- `TxDataAvailable` 从第 4 路 TX ring 取数据，按 509 字节分片，使用硬件端口 ID 7，经 data OUT 串行发送。
- 只有 USB 完整写入后推进 `txCI` 并调用 `TxFreeSpaceAvailable()`；错误时保留未确认数据并停止无界重试。
- data IN 持续异步读取，解码 32 字节 RX records，只把 hardware port 7 数据写入该 RX ring，并调用 `RxDataAvailable()`。
- RX ring 无空间时背压，不覆盖数据；拔出或 Stop 后 completion 不访问已释放对象。

**TDD：**

1. 在 fake transport + ring fixture 下写 TX 分片、短写不提交、RX 回绕、非第 4 路丢弃、停止后 completion 安全测试，先观察 RED。
2. 最小实现到 GREEN。
3. 全部 CTest、无签名 DEXT 构建、已有 libusb 硬件测试通过。
4. 提交：`feat: 实现 CH9344 单路串口数据收发`。

## 阶段 7：签名、激活和第 4 路标准节点验收

**文件：**

- 新建 `tests/driverkit/test_unsigned_bundle.sh`
- 新建 `tests/driverkit/test_activation.sh`
- 新建 `tests/driverkit/test_serial_nodes.sh`
- 新建 `tests/driverkit/test_port4_tty_loopback.py`
- 修改 `docs/verification/tdd/2026-08-31-driverkit.md`

**顺序：**

1. 先用 unsigned bundle 测试检查 DEXT 嵌入位置、动态依赖中无 libusb、Info/entitlement 一致。
2. 检查 Developer Mode、SIP、签名 identity、App ID/provisioning profile；任何缺失都记录准确命令和错误，不通过修改安全边界绕过。
3. 签名并由宿主提交激活，验收 `systemextensionsctl list` 和 `ioreg` 绑定到 `1a86:e018`。
4. 先写节点测试并确认在 DEXT 未激活时 RED；激活后要求出现且只出现第 4 路原型节点。
5. 先写 `/dev/cu.*` 二进制回环测试，测试 payload 包含 `00 7f ff`、16 字节、509 字节，确认旧状态 RED；实现/修复最小 DriverKit 路径到 GREEN。
6. 提交：`test: 验证 CH9344 单路 DriverKit 回环`。

如果 entitlement/provisioning profile 阻塞激活，本阶段只能提交可复核阻塞证据；不得把无签名构建成功描述为驱动已安装。

## 阶段 8：共享 transport 创建四路串口服务

**文件：**

- 将 DEXT 拆分为 `CH9344Transport.iig/.cpp` 与 `CH9344SerialPort.iig/.cpp`
- 修改 `Driver/Extension/Info.plist`，增加四份端口 child-service 属性
- 新建 `Driver/Shared/PortScheduler.*`
- 新建 `tests/driverkit/PortSchedulerTests.cpp`
- 修改 `CMakeLists.txt`

**行为：**

- transport 是唯一 USB interface owner；用 `IOService::Create()` 创建逻辑端口 0～3。
- 四路名称和 suffix 稳定且唯一，用户界面显示端口 1～4。
- RX 按 hardware port 4～7 解复用到唯一端口。
- TX 使用轮转公平调度；一个繁忙端口不能永久饿死其他端口。
- 任一端口创建失败时回收全部端口并终止 transport；Stop 先停端口再关闭 USB。

**TDD：**

1. 先写四端口路由、交错 RX、轮转公平、端口失败全回滚、停止顺序测试，观察 RED。
2. 最小 scheduler/registry model 到 GREEN。
3. adapter 编译通过后激活新版 DEXT；节点测试从“只允许 1 个”更新为“必须恰好 4 个”，先观察旧实现失败，再使其通过。
4. 提交：`feat: 发布 CH9344 四路串口服务`。

## 阶段 9：最终硬件与生命周期验收

**文件：**

- 修改 `tests/driverkit/test_serial_nodes.sh`
- 修改 `tests/driverkit/test_port4_tty_loopback.py`
- 新建 `tests/driverkit/test_replug.sh`
- 新建 `docs/verification/2026-08-31-final-driver.md`
- 修改 `README.md`

**验收矩阵：**

- 四对 `/dev/cu.*`、`/dev/tty.*` 节点稳定出现，suffix 1～4 唯一。
- 四路可分别打开、设置 9600/115200/921600 和 8N1、关闭。
- 第 4 路在唯一短接条件下通过 1、16、509 字节二进制回环。
- 第 4 路 RX 不进入第 1～3 路；第 1～3 路无短接，不要求收到回环。
- DTR/RTS 命令通过 DEXT USB 路径提交；当前无电平仪器，不宣称电气波形通过。
- 拔出后四路消失；重新插入后四路恢复且第 4 路再次回环通过。
- 宿主可停用和重新激活 DEXT，失败时显示可诊断状态。
- 最终 bundle 不依赖 Homebrew/MacPorts/libusb，安装和卸载步骤可复现。

所有系统测试串行通过后，运行完整 CTest、无签名构建、签名构建、bundle 检查和 `git diff --check`，提交：`test: 完成 CH9344 四路驱动系统验收`。

## 完成定义

只有阶段 1～9 全部完成、签名 DEXT 在当前机器激活、四路节点存在、第 4 路标准串口回环和拔插恢复通过，才可以称为“最终驱动完成”。若 Apple entitlement、用户批准或当前系统安全配置阻塞激活，必须明确标记为外部阻塞，并保留已经通过的源码、编译、bundle 和协议证据。
