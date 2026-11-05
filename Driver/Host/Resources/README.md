# OpenCH93xxSer App icon

`OpenCH93xxSer-1024.png` 是带透明边缘的 1024 × 1024 图标原图；`AppIcon.icns` 包含 macOS 16、32、128、256、512 点的 1x/2x 表示，由 Host Resources 构建阶段复制到 App。

图形为深蓝圆角底板、银色芯片和四路青色信号。使用内置 image_gen 工具生成，再通过 macOS `sips` 缩放、`iconutil` 封装，未调用外部 CLI/API。

生成提示词：

```text
Use case: logo-brand
Asset type: production macOS application icon, square 1024 by 1024 pixels.
Primary request: An original icon for OpenCH93xxSer, a USB to four-channel serial driver.
Subject: A bold, simple silver microchip connected to four distinct luminous serial signal traces. The four traces form an elegant, balanced motif and terminate in four round connection points. Make the silhouette legible at 32 pixels. No lettering.
Composition: One centered icon tile occupying approximately 86% of the canvas with consistent margin, macOS rounded-square silhouette; fully transparent outside the tile. View directly from the front. No mockup or surrounding interface.
Style: Refined native desktop app icon, dark midnight blue enamel tile, restrained cyan highlights on the four signals, silver chip with soft dimensional bevels, clean geometry, subtle top lighting. Simple, sculpted, professional.
Constraints: Exactly one icon. Real alpha transparency outside the rounded tile; tile itself opaque. Do not draw a checkerboard or a background behind the icon. Avoid text, logos of other companies, USB trident logos, fine circuitry, visual clutter, dramatic neon bloom, perspective tilt or a large drop shadow.
```

重新封装 ICNS（在仓库根目录运行）：

```bash
mkdir -p build/AppIcon.iconset
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" Driver/Host/Resources/OpenCH93xxSer-1024.png \
    --out "build/AppIcon.iconset/icon_${size}x${size}.png"
  sips -z "$((size * 2))" "$((size * 2))" Driver/Host/Resources/OpenCH93xxSer-1024.png \
    --out "build/AppIcon.iconset/icon_${size}x${size}@2x.png"
done
iconutil -c icns build/AppIcon.iconset -o Driver/Host/Resources/AppIcon.icns
```
