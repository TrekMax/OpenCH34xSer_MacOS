# OpenCH93xxSer

面向 WCH CH9344 的原生 macOS 四路串口驱动，基于 DriverKit，支持最高 12 Mbps、8N1、DTR 和 RTS。

当前提供开发签名版本，适用于已注册的开发设备，尚未提供公证发行版。四路枚举已验证，实际收发回环仅验证第 4 路，详见[验收记录](docs/verification/2026-11-05-opench93xx-branding.md)。

## 安装使用

1. 将已签名的 `OpenCH93xxSer.app` 放入 `/Applications`。
2. 打开 App，点击“激活驱动”，按 macOS 提示完成授权；若提示重启，先重启。
3. 重新插拔 CH9344，即可使用四路标准串口。

节点名称为 `/dev/cu.serialCH93xxUSB0-<ID>` 至 `/dev/cu.serialCH93xxUSB3-<ID>`，并提供对应的 `tty` 节点。末尾 ID 随枚举变化。连接设备并激活驱动后，可在仓库根目录查询实时映射：

```bash
python3 tests/driverkit/serial_nodes.py
```

## 文档

- [构建、签名安装与验证](docs/development.md)
- [驱动架构与技术选型](docs/architecture.md)
- [build 8 实机验收与安装包信息](docs/verification/2026-11-05-opench93xx-branding.md)
- App 图标：[PNG](Driver/Host/Resources/OpenCH93xxSer-1024.png) · [ICNS](Driver/Host/Resources/AppIcon.icns) · [制作说明](Driver/Host/Resources/README.md)
