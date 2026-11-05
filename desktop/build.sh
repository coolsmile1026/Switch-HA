#!/bin/sh
# macOS 桌面预览构建（只用于调 UI，不是 Switch 产物）
set -e
cd "$(dirname "$0")/.."

V="vendor"
CFLAGS="-O2 -Wall -I source -I $V/SDL2.framework/Headers -I $V/SDL2_ttf.framework/Headers"
LIBS="-F$V -framework SDL2 -framework SDL2_ttf -Wl,-rpath,@executable_path/../$V"

cc $CFLAGS \
   source/gfx.c source/ui.c source/applog.c source/json.c \
   source/net.c source/update.c \
   desktop/mock_ha.c desktop/cam_stub.c desktop/preview.c \
   -o desktop/preview $LIBS -lcurl

echo "built desktop/preview"
