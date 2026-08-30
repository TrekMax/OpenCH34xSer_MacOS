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
