#!/bin/sh
# Switch-HA 一键发版：编译 -> 复制到 dist/ -> 建 tag + Release 并上传 nro
#
# 用法：
#   ./tools/release.sh                 只编译打包，产物在 dist/
#   ./tools/release.sh --publish       额外在当前仓库创建 GitHub Release
#
# 认证：优先用环境变量 GITHUB_TOKEN；没有则从 macOS 钥匙串取
# （首次用 `gh auth login` 或 `git credential approve` 存入）。
# 依赖：docker（镜像 mihome-builder）、curl；--publish 还需 git 且 remote 已配置
set -e

OWNER=coolsmile1026
REPO=coolsmile1026/Switch-HA
TOPDIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOPDIR"

VER=$(sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' Makefile | tr -d ' \t\r')
[ -n "$VER" ] || { echo "读不到 Makefile 里的 APP_VERSION"; exit 1; }
TAG="v$VER"
ASSET="switch-ha-$TAG.nro"

echo "==> 版本 $VER  (tag $TAG)"

# ---------- 1. 编译 ----------
docker run --rm --user "$(id -u):$(id -g)" -v "$TOPDIR":/src -w /src \
    mihome-builder make

mkdir -p dist
cp switch-ha.nro "dist/$ASSET"
echo "==> 产物 dist/$ASSET  ($(wc -c < "dist/$ASSET") 字节)"

# ---------- 2. 校验元数据 ----------
python3 - "$TOPDIR" "$VER" <<'PY'
import sys, struct
root, ver = sys.argv[1], sys.argv[2]
d = open(root + "/switch-ha.nro", "rb").read()
base = 0x10
assert d[base:base+4] == b"NRO0", "不是 NRO0"
A = d.find(b"ASET")
assert A > 0, "找不到 ASET 段"
ioff, isize, noff, nsize = struct.unpack_from("<QQQQ", d, A + 8)
N = A + noff
title   = d[N:N+0x200].split(b"\0")[0].decode("utf8", "replace")
author  = d[N+0x200:N+0x400].split(b"\0")[0].decode("utf8", "replace")
version = d[N+0x3060:N+0x3070].split(b"\0")[0].decode("utf8", "replace")
icon = d[A+ioff:A+ioff+4]
print(f"    title={title!r} author={author!r} version={version!r} icon={isize}B {icon.hex()}")
assert version == ver, f"NACP 版本 {version} != Makefile {ver}"
assert icon == b"\xff\xd8\xff\xe0", "图标不是 JPEG"
print("    元数据校验通过")
PY

# ---------- 3. 发布 ----------
if [ "$1" != "--publish" ]; then
    echo "==> 未发布。手动上传：GitHub -> Releases -> Draft a new release"
    echo "    tag: $TAG   附件: dist/$ASSET"
    exit 0
fi

TOKEN="$GITHUB_TOKEN"
if [ -z "$TOKEN" ]; then
    # stdin 是上面的管道，凭据不存在时读到 EOF 会直接报错退出，不会挂住
    TOKEN=$(printf 'protocol=https\nhost=github.com\n\n' \
        | git credential fill 2>/dev/null \
        | sed -n 's/^password=//p')
fi
if [ -z "$TOKEN" ]; then
    echo "没有 GitHub 凭据：设 GITHUB_TOKEN，或先把令牌存进钥匙串"
    echo "  printf 'protocol=https\\nhost=github.com\\nusername=你的账号\\npassword=令牌\\n' | git credential approve"
    exit 1
fi
AUTH="Authorization: Bearer $TOKEN"

git rev-parse --git-dir >/dev/null 2>&1 || { echo "当前目录不是 git 仓库"; exit 1; }
if ! git diff-index --quiet HEAD --; then
    echo "工作区有未提交改动，先提交再发版"; exit 1
fi

API="https://api.github.com/repos/$REPO"

echo "==> 推 tag $TAG"
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "    tag $TAG 已存在，跳过（如需重发请先删除远端 tag）"
else
    git tag "$TAG"
    git push origin "$TAG"
fi

echo "==> 创建 Release"
REL_ID=$(curl -sS -X POST -H "$AUTH" -H "Accept: application/vnd.github+json" \
    "$API/releases" \
    -d "{\"tag_name\":\"$TAG\",\"name\":\"Switch HA $TAG\",\"draft\":false,\"prerelease\":false,\"target_commitish\":\"main\"}" \
  | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('id') or sys.exit(d.get('message','创建失败')))")
echo "    release id=$REL_ID"

echo "==> 上传 $ASSET"
curl -sS -X POST -H "$AUTH" -H "Content-Type: application/octet-stream" \
    --data-binary "@dist/$ASSET" \
    "https://uploads.github.com/repos/$REPO/releases/$REL_ID/assets?name=$ASSET" \
  | python3 -c "import sys,json; d=json.load(sys.stdin); print('    ', d.get('browser_download_url') or d)"

echo "==> 完成： https://github.com/$REPO/releases/tag/$TAG"
