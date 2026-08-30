# CH9344 macOS Driver

面向 WCH CH9344（USB VID `0x1a86`、PID `0xe018`）的 macOS 四路串口驱动验证与实现工程。

当前目标是在本机完成以下能力验证：

- 识别 CH9344 USB 设备及其四端口复用协议；
- 发布四个相互独立的标准串口服务；
- 支持波特率、8N1、RX、TX、DTR 和 RTS；
- 使用第 4 路 TX/RX 短接完成首个硬件回环验收。

项目采用测试驱动开发（TDD），所有功能遵循 RED、GREEN、REFACTOR 循环，并按阶段使用中文 Conventional Commits 提交。

## 当前状态

工程基线初始化中。评估、设计、实施计划和验证证据将记录在 `docs/` 下。
