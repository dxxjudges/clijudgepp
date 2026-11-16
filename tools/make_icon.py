#!/usr/bin/env python3
# make_icon.py
# 生成 CliJudge++ 各平台图标：Windows .ico、Android mipmap、站点图标。
# 用法：python tools/make_icon.py
# 依赖：Pillow

import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024  # 主尺寸

# 配色与站点一致
C_TL = (92, 141, 255)    # #5C8DFF
C_TR = (123, 92, 255)    # #7B5CFF
C_BL = (63, 123, 245)    # #3F7BF5
C_BR = (109, 92, 255)    # #6D5CFF

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\arialbd.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
]


def load_font(size):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    raise SystemExit("找不到粗体字体，请安装 Arial Bold 或 DejaVu Sans Bold")


def gradient(size):
    """四角插值放大，得到平滑的对角渐变。"""
    small = Image.new("RGB", (2, 2))
    small.putpixel((0, 0), C_TL)
    small.putpixel((1, 0), C_TR)
    small.putpixel((0, 1), C_BL)
    small.putpixel((1, 1), C_BR)
    return small.resize((size, size), Image.BICUBIC)


def rounded_mask(size, radius, ss=4):
    """超采样绘制圆角矩形遮罩，边缘更干净。"""
    big = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        [0, 0, size * ss - 1, size * ss - 1], radius=radius * ss, fill=255
    )
    return big.resize((size, size), Image.LANCZOS)


def draw_monogram(img, tracking=6):
    """白色 CJP 字标，按字符逐个绘制以控制字距，按实际墨迹包围盒居中。"""
    font = load_font(int(S * 0.335))
    draw = ImageDraw.Draw(img)

    widths = [draw.textlength(ch, font=font) for ch in "CJP"]
    total = sum(widths) + tracking * (len(widths) - 1)

    # 先画到透明层，再按非透明像素的包围盒精确居中（避开字体上下留白）
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    x = 0.0
    for ch, w in zip("CJP", widths):
        ld.text((x, 0), ch, font=font, fill=(255, 255, 255, 255))
        x += w + tracking

    bbox = layer.getbbox()
    ink_w = bbox[2] - bbox[0]
    ink_h = bbox[3] - bbox[1]
    dx = (S - ink_w) / 2.0 - bbox[0]
    # 光学居中：字标略高于几何中心约 1.5%
    dy = (S - ink_h) / 2.0 - bbox[1] - S * 0.015

    img.alpha_composite(layer.transform(layer.size, Image.AFFINE,
                                        (1, 0, -dx, 0, 1, -dy),
                                        resample=Image.BICUBIC))


def build_master():
    img = gradient(S).convert("RGBA")
    draw_monogram(img)

    # 内描边：让图标在深浅背景上都有轮廓
    overlay = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    inset = 10
    od.rounded_rectangle(
        [inset, inset, S - inset - 1, S - inset - 1],
        radius=int(S * 0.2),
        outline=(255, 255, 255, 46),
        width=6,
    )
    img = Image.alpha_composite(img, overlay)

    mask = rounded_mask(S, int(S * 0.22))
    img.putalpha(mask)
    return img


def write_runtime_header(master):
    """把 64×64 RGBA 像素内嵌成头文件，供 SDL_SetWindowIcon 使用。"""
    px = 64
    data = master.resize((px, px), Image.LANCZOS).tobytes()

    lines = [
        "// icon_data.h",
        "// 由 tools/make_icon.py 生成，请勿手工修改。",
        "",
        "#ifndef CLIJUDGE_GUI_ICON_DATA_H",
        "#define CLIJUDGE_GUI_ICON_DATA_H",
        "",
        "namespace clijudge {",
        "namespace gui {",
        "",
        "inline constexpr int kIconSize = %d;" % px,
        "",
        "// RGBA8888，行优先，自上而下",
        "inline const unsigned char kIconRgba[] = {",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ",".join(str(b) for b in chunk) + ",")
    lines += ["};", "", "} // namespace gui", "} // namespace clijudge", "",
              "#endif // CLIJUDGE_GUI_ICON_DATA_H", ""]

    out = os.path.join(ROOT, "src", "icon_data.h")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", out)


def main():
    master = build_master()
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)

    # 站点用 512 图
    site_png = os.path.join(ROOT, "docs", "icon-512.png")
    master.resize((512, 512), Image.LANCZOS).save(site_png, "PNG")
    print("wrote", site_png)

    # Windows 图标（多尺寸 ICO）
    ico = os.path.join(ROOT, "assets", "icon.ico")
    master.save(ico, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                                          (64, 64), (128, 128), (256, 256)])
    print("wrote", ico)

    # PNG 主图（Linux 分发 / 文档用）
    png = os.path.join(ROOT, "assets", "icon.png")
    master.resize((512, 512), Image.LANCZOS).save(png, "PNG")
    print("wrote", png)

    # 运行时窗口图标（SDL 无 SDL_image，需内嵌像素数据）
    write_runtime_header(master)

    # Android mipmap
    res = os.path.join(ROOT, "android", "app", "src", "main", "res")
    for folder, px in (("mipmap-mdpi", 48), ("mipmap-hdpi", 72), ("mipmap-xhdpi", 96),
                       ("mipmap-xxhdpi", 144), ("mipmap-xxxhdpi", 192)):
        out = os.path.join(res, folder, "ic_launcher.png")
        if not os.path.isdir(os.path.dirname(out)):
            os.makedirs(os.path.dirname(out), exist_ok=True)
        master.resize((px, px), Image.LANCZOS).save(out, "PNG")
        print("wrote", out)

    return 0


if __name__ == "__main__":
    sys.exit(main())
