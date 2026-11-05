# 驱动架构与技术选型

[返回 README](../README.md)

面向 WCH CH9344（USB VID `0x1a86`、PID `0xe018`）的原生 macOS 四路串口 DriverKit 工程。

## 工程组成

- Swift 宿主 App：使用 SystemExtensions 框架激活、升级和停用 DEXT；
- C++17 DriverKit DEXT：独占 CH9344 interface 0，使用 USBDriverKit 和 SerialDriverKit 发布四路标准串口；
- C++17 共享核心：CH9344 协议、端点识别、环形队列、四路 RX 路由、TX 公平调度和 USB 启停状态机；
- libusb probe：只用于驱动激活前的协议和硬件验证，不是最终驱动的运行时依赖。

## 数据通路

```text
CH9344 USB device -> CH9344Device 选择 configuration 1
        |
        v
CH9344 USB interface 0
        |
        v
CH9344Transport (唯一 USB interface / pipe owner)
        |
        +-- CH9344Driver port 1 -> /dev/cu.serialCH93xxUSB0-<ID>
        +-- CH9344Driver port 2 -> /dev/cu.serialCH93xxUSB1-<ID>
        +-- CH9344Driver port 3 -> /dev/cu.serialCH93xxUSB2-<ID>
        +-- CH9344Driver port 4 -> /dev/cu.serialCH93xxUSB3-<ID>
```

四路共享 command IN `0x81`、data IN `0x82`、command OUT `0x01`、data OUT `0x02`。RX 32 字节记录按 hardware port 4～7 解复用；TX 以最多 509 字节 payload 组帧并轮转调度。当前支持 baud、8N1、RX、TX、DTR 和 RTS，不包含 CH348、GPIO、RS-485、9-bit 或硬件流控。

## 为什么 DEXT 使用 C++

DriverKit 的驱动类通过 IIG 接口定义并以 C++ 实现；USBDriverKit 和 SerialDriverKit 的驱动侧 API 也是 C++ API。因此 DEXT 采用 C++17 是平台原生、接口最直接且最容易审计的选择。

Swift 适合宿主 App 和 `OSSystemExtensionRequest` 生命周期，本工程已经这样使用。Rust 没有 Apple 官方 DriverKit/IIG 绑定；可以把纯协议核心封装为 Rust 静态库，但仍需 C++ adapter，并会额外引入 ABI、panic、分配器和 DriverKit 运行时验证成本，所以当前范围不采用。

## 为什么诊断命令叫 lsusb

`cyme --lsusb` 的名称和输出格式沿用了 Linux `lsusb` 的使用习惯，但当前 macOS 版本的 cyme 直接链接 Apple IOKit 与 CoreFoundation。它调用的是 macOS 原生 USB 枚举能力，不需要 Linux USB 子系统，也不代表驱动必须使用 libusb。

本工程里的 libusb probe 同样只承担早期用户态协议验证。最终 Host 和 DEXT 的动态依赖中没有 libusb，USB I/O 全部使用 USBDriverKit。
