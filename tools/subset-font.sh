#!/bin/sh
# 字体子集化：把 romfs/fonts/main.otf 砍到 OTA 友好的体积。
#
# 保留：ASCII、拉丁补充（° × ÷）、常用标点/箭头/带圈数字/几何图形/杂项符号、
#       CJK 标点、日文假名、GB2312 全部 6763 个汉字、全角字符
# 丢弃：CJK 扩展区生僻字、GB2312 之外的汉字（GBK/GB18030 扩展）、韩文、
#       以及 GSUB/GPOS 等排版特性表（SDL2_ttf 走 FreeType 基本排版，用不到）
#
# 效果：完整版 16MB -> 4.8MB（CJK 全集）-> 1.5MB（GB2312 常用字）
# 取舍：家用 HA 的实体名/地区名基本都在 GB2312 范围内；万一遇到生僻字
#       会显示成方块，需要的话把下面 --text-file 去掉换回 U+4E00-9FFF。
#
# 注意：完整版备份固定放在仓库根目录 main.otf.full。
#       千万不要放进 romfs/ —— romfs 打包整个目录，会把 16MB 原字体
#       一起塞进 NRO（亲测 13.9MB 直接变 29.5MB）。
#
# 依赖：python3 -m pip install --user fonttools
set -e
cd "$(dirname "$0")/.."
F=romfs/fonts/main.otf
FULL=main.otf.full
[ -f "$FULL" ] || { echo "找不到 $FULL（完整版备份）"; exit 1; }

# 生成 GB2312 汉字表（6763 字）：不用手挂码表，直接让 python 解码一遍
python3 - <<'PY'
chars = []
for b1 in range(0xB0, 0xF8):
    for b2 in range(0xA1, 0xFF):
        try:
            chars.append(bytes([b1, b2]).decode("gb2312"))
        except UnicodeDecodeError:
            pass
open("/tmp/switch-ha-gb2312.txt", "w").write("".join(chars))
print("GB2312 汉字 %d 个" % len(chars))
PY

python3 -m fontTools.subset "$FULL" --output-file="$F.new" \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2000-206F,U+2190-21FF,U+2460-24FF,U+25A0-25FF,U+2600-27BF,U+3000-303F,U+3040-30FF,U+FF00-FFEF" \
  --text-file=/tmp/switch-ha-gb2312.txt \
  --layout-features=kern --no-hinting --name-IDs='*'
mv "$F.new" "$F"

echo "完成：$F $(du -h "$F" | cut -f1)（完整版始终保留在 $FULL）"
echo "验证：./desktop/build.sh && ./desktop/preview --screen home --shot /tmp/f.bmp"
