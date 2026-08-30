# CH9344 macOS 最终驱动交付状态

日期：2026-08-31

## 结论

四路原生 DriverKit 驱动代码、Swift 安装宿主、TDD 核心测试、unsigned bundle 和第 4 路协议硬件回环均已完成。当前不能把驱动标记为“已安装/最终验收通过”，唯一已确认的外部阻塞是本机 Xcode 没有可用于工程 Team 的开发者账号与 DriverKit provisioning profile。

## 已交付实现

- 精确匹配 `0x1a86:0xe018`、configuration 1、interface 0。
- `CH9344Transport` 独占一组 USB pipe 并只初始化一次设备。
- 通过 `IOService::Create()` 发布四个 `IOUserSerial` 子服务，suffix 固定为 1～4。
- 四路各自支持 baud、8N1、RX、TX、DTR、RTS。
- RX 按 hardware port 4～7 解复用；单路背压不重复投递已完成端口。
- TX 采用轮转调度；只有 USB 完整成功后提交对应端口 `txCI`。
- Stop/拔出路径先停止子服务，再同步取消数据 I/O，最后释放 pipe 和关闭 interface。
- Host 通过 SystemExtensions 框架提交激活、停用和升级请求。

## 已通过证据

| 验证 | 结果 |
| --- | --- |
| CMake/C++ 构建 | 通过 |
| 默认 CTest | 10/10 通过 |
| DriverKit unsigned clean build | 通过，无警告 |
| DEXT 动态依赖 | DriverKit、USBDriverKit、SerialDriverKit，无 libusb |
| USB inspect | CH9344Q `0x42`，四个 512-byte Bulk endpoint |
| 第 4 路 libusb 回环 | 9600/115200/921600 与 1/16/509 字节通过 |
| 签名构建 | 阻塞 |
| DEXT active/enabled | 未完成 |
| 四对标准串口节点 | 未完成 |
| 第 4 路标准 TTY 回环 | 未完成 |
| 重插拔/重启恢复 | 未完成 |

## 签名阻塞证据

本机有一个有效 codesigning identity：

```text
Apple Development: TianShuang Ke (M6D66QWKAN)
```

但本机唯一 profile 属于 Team `9BT9NV52NL`，是 iOS 通配 profile，不包含 DriverKit entitlement。

普通签名构建退出码 65：

```text
No profiles for 'com.trekmax.OpenCH34xSer.driver' were found
No profiles for 'com.trekmax.OpenCH34xSer' were found
```

加入 `-allowProvisioningUpdates` 后仍退出码 65：

```text
No Accounts: Add a new account in Accounts settings.
```

SIP 保持 enabled。没有关闭系统安全能力，也没有使用 ad-hoc 签名冒充 DriverKit entitlement。

## 恢复验收的最短路径

1. 在 Xcode Settings > Accounts 登录 Team `M6D66QWKAN` 的有效账号。
2. 在 Apple Developer 后台确认 DriverKit/serial/USB transport entitlement 已批准。
3. 为 Host 与 DEXT 两个 identifier 创建或允许 Xcode 下载 development profile。
4. 重新执行 README 的签名构建、宿主激活和 `CH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS=ON` 矩阵。
5. 只有 activation、四节点、第 4 路 TTY 回环和 replug 全部通过后，才能将最终驱动状态改为完成。
