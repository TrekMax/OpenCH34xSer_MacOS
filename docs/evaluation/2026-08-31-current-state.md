# CH9344 macOS 驱动现状评估

日期：2026-08-31

评估环境：macOS 27.0（Build 26A5421a），Apple Silicon，Xcode 27.0 beta（27A5194q）

## 结论

USB 硬件、线缆和系统 USB 枚举链路工作正常。当前没有串口节点的直接原因是：已经启用的 WCH `cn.wch.CH34xVCPDriver` Driver Extension 不包含 VID `0x1a86`、PID `0xe018` 的匹配人格，因此不会绑定这颗 CH9344 设备。

参考 WCH 官方 Linux 驱动可以确认 `0x1a86:0xe018` 是 CH9344，并取得四路复用的协议行为。自行实现 macOS 驱动在技术上可行，但不能把 CH9344 当作四个独立 USB interface 的普通 USB 转串口芯片处理：它在同一 USB interface 的共享端点上复用四路 UART。工程必须先验证协议，再验证 DriverKit 能否从一个 USB transport 发布四个标准串口服务。

## 当前机器证据

### USB 设备已经枚举

执行：

```console
$ cyme --lsusb
Bus 000 Device 001: ID 1a86:8095 QinHeng Electronics USB Hub
Bus 000 Device 002: ID 1a86:8095 QinHeng Electronics USB Hub
Bus 000 Device 003: ID 1a86:e018 QinHeng Electronics USB2.0 To Multi Serial Ports
```

`ioreg -p IOUSB -l -w 0` 同时报告：

| 属性 | 当前值 |
| --- | --- |
| `idVendor` | 6790（`0x1a86`） |
| `idProduct` | 57368（`0xe018`） |
| `USB Product Name` | `USB2.0 To Multi Serial Ports` |
| `bDeviceClass` | 255 |
| `bDeviceSubClass` | 128 |
| `bDeviceProtocol` | 55 |
| `UsbLinkSpeed` | 480000000 |

这证明设备已到达 `IOUSBHostDevice`，问题不在“系统完全看不到 USB 设备”这一层。

### WCH Driver Extension 已启用但未绑定该 PID

执行 `systemextensionsctl list` 得到：

```text
*  *  5JZGQTGU4W  cn.wch.CH34xVCPDriver (1.0/1)  cn.wch.CH34xVCPDriver  [activated enabled]
```

已安装 DEXT 是包含 `arm64` 和 `x86_64` 的通用二进制，签名主体为 WCH。其 `Info.plist` 使用 `IOUserSerial` 匹配多种 WCH 芯片，但执行下列精确查询：

```console
$ plutil -convert json -o - <DEXT>/Info.plist \
    | jq '[.IOKitPersonalities[] \
        | select(.idVendor == 6790 and .idProduct == 57368)] | length'
0
```

也就是说，当前驱动中匹配 `0x1a86:0xe018` 的人格数量是零。驱动处于激活状态不等于它支持当前设备。

### 没有生成目标串口节点

执行 `ls -l /dev/tty.* /dev/cu.*` 只得到系统自带节点：

```text
/dev/cu.Bluetooth-Incoming-Port
/dev/cu.debug-console
/dev/tty.Bluetooth-Incoming-Port
/dev/tty.debug-console
```

没有 WCH 或 CH9344 对应的 `/dev/cu.*`、`/dev/tty.*` 节点，与“没有匹配人格”的判断一致。

## Linux 驱动提供的协议事实

参考仓库：<https://github.com/WCHSoftGroup/ch9344ser_linux>

评估提交：`0450213977f8a9acc8b9ccc70754efed0842d1d0`

参考文件：`driver/ch9344.c`

已确认的最小协议事实如下：

- USB ID 表把 `0x1a86:0xe018` 标记为 CH9344；
- PID `0xe018` 使用 4 个逻辑串口，硬件端口编号偏移为 4；
- interface 0 提供共享的数据 Bulk IN/OUT 和命令 Bulk IN/OUT 端点；
- RX 按 32 字节记录解析：字节 0 是硬件端口号 `4...7`，字节 1 是不超过 30 的载荷长度，载荷从字节 2 开始；
- TX 记录头为 `[硬件端口号, 长度低字节, 长度高字节]`，载荷从字节 3 开始；
- Linux 实现还包含芯片枚举、波特率/数据格式、DTR/RTS、Break、流控和 modem status 命令路径。

逻辑端口映射必须固定为：

| 用户端口 | 协议硬件端口号 | 当前物理条件 |
| --- | ---: | --- |
| 第 1 路 | 4 | 未短接 TX/RX |
| 第 2 路 | 5 | 未短接 TX/RX |
| 第 3 路 | 6 | 未短接 TX/RX |
| 第 4 路 | 7 | TX/RX 已短接 |

## macOS 实现判断

### 为什么不能只给现有驱动补一个 PID

现有 WCH DEXT 的人格以“一个 USB interface 对应一个 `IOUserSerial`”为主。CH9344 则是在一个 interface、两组共享 Bulk 端点中携带端口号完成四路复用。即使给现有二进制的匹配表增加 `0xe018`，二进制内部也没有已证明的 CH9344 复用解析逻辑，而且修改签名包会破坏代码签名，不能构成可维护方案。

### 推荐架构

工程采用三层结构：

1. 与平台无关的 C++ 协议核心负责端口映射、TX 组帧、RX 解帧和命令编码；
2. 用户态 USB 验证工具负责独占 interface、发现四个端点并在第 4 路执行真实回环；
3. DriverKit transport 复用同一协议核心，并向 SerialDriverKit 发布四个逻辑串口服务。

`IOUserUSBSerial` 自带单路 USB 串口数据泵，且把 `TxDataAvailable` 标记为 `final`，不适合直接承担共享端点上的四路 TX 调度。DriverKit 阶段优先评估“自定义 USB transport + 四个 `IOUserSerial` 服务”；若公共 DriverKit API 不能稳定发布四个子串口服务，再把用户态 PTY 桥接作为诊断备选，而不是最终交付。

## TDD 与硬件验收边界

所有可纯函数化的协议行为先使用自动化测试完成 RED、GREEN、REFACTOR：

- 端口 `0...3` 到硬件端口 `4...7` 的映射和越界拒绝；
- TX 单帧及分片组帧；
- RX 单帧、多帧、截断、非法端口和非法长度处理；
- 波特率、8N1、DTR 和 RTS 命令编码；
- 四路队列隔离及断开重连状态复位。

硬件测试按当前接线分级：

- 第 4 路可以验证真实 TX→RX 回环、不同波特率、二进制数据和持续传输；
- 第 1～3 路只能验证节点创建、独立配置、TX 路由和不发生跨端口 RX，不能宣称回环通过；
- 仅短接 TX/RX 无法验证 DTR/RTS 引脚电平。现阶段只能验证控制命令编码和 USB 下发成功；电气验收需要逻辑分析仪、万用表或补充握手线接线。

## 许可证边界

参考 Linux 文件声明为 `GPL-2.0+`。本工程不复制 Linux 驱动实现代码，协议核心根据可观察的帧格式和独立测试重新实现；文档保留参考仓库及提交号。若未来需要分发产品，仍需在发布前进行一次正式许可证与 DriverKit entitlement 审核。

## 当前范围

首期只支持当前机器上的 CH9344：

- VID `0x1a86`、PID `0xe018`；
- 四个标准串口；
- 波特率、8N1、RX、TX、DTR、RTS；
- 不包含 CH348、GPIO、RS-485 扩展和公证分发。
