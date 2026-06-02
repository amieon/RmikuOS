#!/usr/bin/env bash
set -euo pipefail

ARCH="${1:-loongarch64}"

IMG="target/fs-${ARCH}.img"
ROOT="target/fsroot-${ARCH}"

mkdir -p target
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/etc"

if ! command -v mkfs.ext4 >/dev/null 2>&1; then
  echo "错误: 找不到 mkfs.ext4，请先安装 e2fsprogs" >&2
  echo "Ubuntu: sudo apt install e2fsprogs" >&2
  exit 1
fi

echo "=== 构建 ext4 rootfs ($ARCH) ==="

for f in user/build/${ARCH}/*.bin; do
  [ -e "$f" ] || continue

  base="$(basename "$f" .bin)"

  # 0_00_shell -> shell
  # 1_01_hello -> hello
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"

  cp "$f" "$ROOT/bin/$clean"
done

cat > "$ROOT/etc/motd" <<EOF
Welcome to RmikuOS ext4 rootfs!
EOF

rm -f "$IMG"
truncate -s 16M "$IMG"

# -d 可以把 ROOT 目录内容直接写进 ext4 image，不需要 sudo mount。
mkfs.ext4 -q -F -d "$ROOT" "$IMG"

echo "created $IMG"s