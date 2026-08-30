# CH9344 协议验证 TDD 记录

日期：2026-08-31

## Task 1：逻辑端口映射

### RED

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::mapLogicalPort(unsigned char, unsigned char*)`。
- 判断：测试、头文件和测试目标均已编译，失败由目标行为尚未实现造成。

### GREEN

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 通过，1/1 测试通过，零失败，编译无警告，差异格式检查通过。

## Task 2：发送数据组帧

### RED

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::encodeTxFrame(...)`。
- 判断：单帧、61 字节边界和失败时不写缓冲区的测试已编译，失败由组帧行为尚未实现造成。

### GREEN

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 通过，1/1 测试通过；第 4 路帧头、分片边界与输入拒绝测试均为绿色。
- 重构检查：删除了未由 RED 驱动的超大虚拟包长分支，保留当前 USB 最大包长所需的最小实现。

## Task 3：接收数据解帧

### RED

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::decodeRxTransfer(...)`。
- 判断：单记录、多端口顺序、截断、非法端口、非法长度和指针边界测试均已编译，失败由解帧行为尚未实现造成。

### GREEN

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 通过，1/1 测试通过；端口映射、32 字节步长、30 字节上限和失败前不回调均通过。
- 变异检查：将步长改为 31、端口上界改为 8、载荷起点改为 1 或放宽长度 31 时，现有字面量断言会失败。

## Task 4：芯片版本与初始化命令

### RED 1：芯片版本

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::parseChipVersion(...)`。
- 判断：L/Q 分界、精确 4 字节长度和空指针测试已编译，失败由版本解析尚未实现造成。

### GREEN 1：芯片版本

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build -R '^protocol$' --output-on-failure`
- 退出码：0
- 结果：L/Q 版本分界和响应长度验证通过。

### RED 2：上传模式

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::encodeDeviceInitialization(...)`。
- 判断：Q、L `0x39` 和旧版 L 三种行为已编译，失败由上传模式编码尚未实现造成。

### GREEN 2：上传模式

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build -R '^protocol$' --output-on-failure`
- 退出码：0
- 结果：8 字节上传模式命令和旧版 L 空命令序列均通过。

### RED 3：端口初始化

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::encodePortInitialization(...)`。
- 判断：逻辑端口 0/3 的寄存器基址与越界不写输出测试已编译，失败由端口初始化编码尚未实现造成。

### GREEN 3：端口初始化

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 通过，1/1 测试通过；版本、上传模式和三个端口初始化命令全部为绿色。
- 变异检查：修改 Q 分界 `0x40`、上传阈值 `0x39`、端口寄存器步长或任一命令长度时，字面量测试会失败。

## Task 5：串口配置命令

### RED 1：CH9344L 波特率与 8N1

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::encodeUart8N1(...)`。
- 判断：115200 完整六命令、时钟/分频/超时表和非法输入测试已编译，失败由 UART 配置编码尚未实现造成。

### GREEN 1：CH9344L 波特率与 8N1

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build -R '^protocol$' --output-on-failure`
- 退出码：0
- 结果：L 型六命令、低/高时钟、分频、特殊选择字节和超时表通过。

### RED 2：CH9344Q 直接波特率

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build -R '^protocol$' --output-on-failure`
- 退出码：8
- 预期失败：Q 命令长度实际为 6、预期为 9，且字节 2 实际为 `0x01`、预期为 `0x00`。
- 判断：失败准确证明 Q 型不能复用 L 型分频命令。

### GREEN 2：CH9344Q 直接波特率

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build -R '^protocol$' --output-on-failure`
- 退出码：0
- 结果：Q 型 9 字节命令按小端顺序携带 115200，协议测试恢复绿色。

### RED 3：DTR/RTS 独立控制

- 命令：`cmake --build build --target ch9344_protocol_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::encodeModemControl(...)`。
- 判断：四种 DTR/RTS 布尔组合和越界不写输出测试已编译，失败由 modem control 编码尚未实现造成。

### GREEN 3：DTR/RTS 独立控制

- 命令：`cmake --build build --target ch9344_protocol_tests && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 通过，1/1 测试通过；L/Q 波特率、8N1、DTR 和 RTS 命令均为绿色。
- 变异检查：颠倒 Q 波特率端序、修改 8N1 字节 `0x03`、合并 DTR/RTS 值或改变端口 3 控制寄存器 `0x3c` 时，现有测试会失败。

## Task 6：USB 端点布局

### RED

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target ch9344_endpoint_layout_tests`
- 退出码：2
- 预期失败：arm64 链接阶段找不到 `ch9344::classifyEndpoints(...)`。
- 判断：真实乱序端点及缺失、重复、非 Bulk、零包长、未知端点号和包长不一致测试均已编译，失败由分类行为尚未实现造成。

### GREEN

- 命令：`cmake --build build && ctest --test-dir build --output-on-failure && git diff --check`
- 退出码：0
- 结果：`protocol` 与 `endpoint_layout` 共 2/2 测试通过，真实端点被稳定识别为 Data `0x82/0x02`、Command `0x81/0x01`、最大包长 512。
- 变异检查：交换端点角色、依赖描述符顺序、接受重复项或忽略 IN/OUT 包长差异时，端点测试会失败。

## Task 7：真实设备检查

### RED

- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON && cmake --build build --target ch9344_probe && ctest --test-dir build -R '^hardware_inspect$' --output-on-failure`
- 退出码：8（CTest；探针骨架退出码为 2）
- 预期失败：探针骨架不支持 `inspect`，没有输出设备、端点和芯片版本行。
- 判断：硬件脚本真实执行已构建程序，失败由 inspect 行为尚未实现造成。

### 集成调试证据

- 首次 GREEN 尝试：设备成功打开，但 `libusb_get_active_config_descriptor` 稳定返回 `LIBUSB_ERROR_NOT_FOUND`。
- 复现：MacPorts libusb 1.0.26 与 Homebrew libusb 1.0.30 结果相同，排除单一旧版本缺陷。
- 交叉证据：设备描述符报告一个配置；IORegistry 报告当前配置 1；`cyme -vvvv` 能读取 configuration 1、interface 0 和四个端点。
- 根因边界：macOS libusb 路径不能为该设备返回 active configuration，但按设备描述符使用配置索引 0 可以读取同一配置。
- 最小修复：仅将查询替换为 `libusb_get_config_descriptor(device, 0, ...)`，保留后续 interface/端点完整校验。

### GREEN

- 命令：`cmake --build build --target ch9344_probe && ctest --test-dir build -R '^hardware_inspect$' --output-on-failure`
- 退出码：0
- 结果：真实硬件测试 1/1 通过；输出确认 CH9344Q `0x42`、Data `0x82/0x02`、Command `0x81/0x01`、包长 512。
- 回归：`ctest --test-dir build -L protocol --output-on-failure` 为 2/2 通过。

## Task 8：第 4 路回环命令

### RED

- 命令：`ctest --test-dir build -R '^hardware_loopback_port4$' --output-on-failure`
- 退出码：8（CTest；探针退出码为 2）
- 预期失败：现有程序只接受 `inspect`，对 16 字节十六进制载荷和 509 字节确定性载荷均打印用法并退出。
- 判断：真实硬件测试已覆盖 CLI、512 字节 TX 包边界和多 RX 记录目标，失败由回环行为尚未实现造成。

### 集成调试证据

- 首次 GREEN 尝试：命令与 Data Bulk OUT 均完整传输，但 Data Bulk IN 在 5 秒内没有数据，程序明确超时退出 6。
- libusb 边界日志确认：interface 0 claim 成功；上传模式、端口初始化、Q 型 115200/8N1、DTR/RTS 命令均完整写到 `0x01`；19 字节数据帧完整写到 `0x02`。
- 临时十六进制诊断确认 command IN 返回上传模式及端口 7 配置状态，随后 16 字节回环连续 3 次通过。
- 移除全部诊断日志、重新构建后，16 字节回环再次连续 3 次通过；正式脚本中的 16 字节与 509 字节回环均通过。
- 判断：首次超时属于当前设备初始化期间未再复现的瞬态，尚无证据把它归因于特定命令或代码分支；不保留诊断日志，也不把猜测写成根因。程序保留 5 秒有界超时。

### GREEN

- 命令：`ctest --test-dir build -R '^hardware_loopback_port4$' --output-on-failure`
- 退出码：0
- 结果：真实硬件测试 1/1 通过；16 字节二进制数据与 509 字节单 TX 帧边界均在第 4 路原样返回。
- 隔离条件：RX 解帧只累积逻辑端口 3，逻辑端口 0...2 的记录不会满足回环成功条件。
