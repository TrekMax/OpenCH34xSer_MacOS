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
