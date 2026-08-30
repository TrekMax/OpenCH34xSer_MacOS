# CH9344 第 4 路硬件回环验收

日期：2026-08-31

被测提交：`48ada8b812539ae9c52fff99bc95c2ce0ad3fce7`

## 验收结论

当前 CH9344 的第 4 路在 TX/RX 短接条件下，通过了 9600、115200 和 921600 波特率，以及 1、16 和 509 字节载荷的真实 USB→UART→USB 回环。509 字节案例同时覆盖单个 512 字节 Data Bulk OUT 包的最大协议载荷，以及设备拆成多条 32 字节 RX 记录后的重新组装。

本结论只适用于当前用户态协议验证阶段和当前接线，不代表 DriverKit 串口节点已经实现。

## 环境与设备

| 项目 | 实测值 |
| --- | --- |
| macOS | 27.0（Build 26A5421a） |
| USB ID | `0x1a86:0xe018` |
| 产品字符串 | `USB2.0 To Multi Serial Ports` |
| 芯片 | CH9344Q |
| 芯片版本 | `0x42` |
| interface | 0 |
| Data Bulk IN/OUT | `0x82` / `0x02` |
| Command Bulk IN/OUT | `0x81` / `0x01` |
| 四个端点最大包长 | 512 |
| 接线 | 仅第 4 路 TX 与 RX 短接 |

`build/ch9344-probe inspect` 的输出：

```text
device 1a86:e018
interface 0
data-in 0x82 bulk 512
data-out 0x02 bulk 512
command-in 0x81 bulk 512
command-out 0x01 bulk 512
chip CH9344Q version 0x42
```

## 串行验收矩阵

硬件 interface 不能被两个进程同时 claim，因此以下命令严格串行执行：

```bash
build/ch9344-probe loopback --port 4 --baud 9600 --length 1
build/ch9344-probe loopback --port 4 --baud 115200 --length 16
build/ch9344-probe loopback --port 4 --baud 115200 --length 509
build/ch9344-probe loopback --port 4 --baud 921600 --length 509
```

| 用户端口 | 波特率 | 载荷字节 | 退出码 | 输出 |
| ---: | ---: | ---: | ---: | --- |
| 4 | 9600 | 1 | 0 | `PASS port=4 baud=9600 bytes=1` |
| 4 | 115200 | 16 | 0 | `PASS port=4 baud=115200 bytes=16` |
| 4 | 115200 | 509 | 0 | `PASS port=4 baud=115200 bytes=509` |
| 4 | 921600 | 509 | 0 | `PASS port=4 baud=921600 bytes=509` |

自动化硬件测试还执行固定二进制载荷：

```text
00 01 7f 80 ff 43 48 39 33 34 34 2d 54 44 44 a5
```

它包含 `0x00`、`0x7f`、`0x80` 和 `0xff`，避免只用可打印字符串掩盖二进制通道问题。

## 已验证的数据与控制路径

每次回环依次执行：

1. 读取 4 字节芯片版本；
2. claim interface 0；
3. 向 Command OUT `0x01` 下发上传模式、端口 4 初始化和 CH9344Q 8N1/波特率命令；
4. 独立下发 DTR=true、RTS=true；
5. 在 Data OUT `0x02` 发送硬件端口号 7 的帧；
6. 从 Data IN `0x82` 解码 32 字节记录，只累计逻辑端口 3；
7. 逐字节比较回环数据；
8. 最佳努力下发 DTR=false、RTS=false，释放 interface 0。

协议单元测试覆盖 DTR 与 RTS 的四种独立布尔组合；本次硬件流程确认对应命令完成 USB Bulk OUT 传输。

## 限制与未完成项

- 第 1～3 路没有 TX/RX 短接，因此没有物理 RX 回环结论；
- DTR/RTS 没有连接逻辑分析仪或万用表，因此没有引脚电平结论；
- 目前是 libusb 用户态验证工具，不发布 `/dev/cu.*` 或 `/dev/tty.*`；
- 当前 `/dev` 中仍只有 Bluetooth 与 debug-console 系统节点；
- 首次开发期 GREEN 尝试出现过一次 5 秒 RX 超时，完成命令边界诊断后未再复现；详细过程保存在 TDD 记录中，程序保留有界超时；
- 并行启动两个探针会使后一个 `libusb_claim_interface` 返回 `LIBUSB_ERROR_ACCESS`，硬件测试必须串行。

## 下一阶段门禁

进入 DriverKit 单路原型前，必须继续复用当前已经通过的协议字面量和硬件路径。单路原型首先只发布第 4 路标准串口节点，验证 `/dev/cu.*`、`/dev/tty.*`、115200/8N1 与同一物理回环；不得用 PTY 结果替代 SerialDriverKit 节点。
