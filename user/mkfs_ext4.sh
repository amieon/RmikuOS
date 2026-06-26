#!/usr/bin/env bash
set -euo pipefail

ARCH="${1:-riscv64}"

IMG="target/fs-${ARCH}.img"
ROOT="target/fsroot-${ARCH}"
OVERLAY="user/rootfs"
FAT_IMG="target/fat-${ARCH}.img"

mkdir -p target
rm -rf "$ROOT"

echo "=== 构建 ext4 rootfs ($ARCH) ==="

#先复制用户自定义 rootfs 模板
if [ -d "$OVERLAY" ]; then
  echo "copy rootfs overlay: $OVERLAY -> $ROOT"
  mkdir -p "$ROOT"
  cp -a "$OVERLAY"/. "$ROOT"/
else
  echo "no $OVERLAY, create minimal rootfs"
  mkdir -p "$ROOT"
fi


# 确保基础目录存在
mkdir -p "$ROOT/programs" "$ROOT/bin" "$ROOT/etc" "$ROOT/home" "$ROOT/tmp" "$ROOT/dev" "$ROOT/proc" "$ROOT/tests"

# 如果用户没有提供 motd，就生成默认 motd
if [ ! -f "$ROOT/etc/motd" ]; then
  cat > "$ROOT/etc/motd" <<EOF
Welcome to RmikuOS ext4 rootfs!
EOF
fi

# 把编译出来的用户程序放进 /bin

for f in user/build/${ARCH}/bin/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')" 
  cp "$f" "$ROOT/bin/$clean"
done


for f in user/build/${ARCH}/tests/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"          
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"  
  cp "$f" "$ROOT/tests/$clean"
done


for f in user/build/${ARCH}/programs/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"
  cp "$f" "$ROOT/programs/$base"
done

# 5. 简单展示 rootfs 内容
echo "rootfs content:"
find "$ROOT" -maxdepth 3 -print | sort

# 6.fs content:"
find "$ROOT" -maxdepth 3 -print | sort
rm -f "$IMG"
truncate -s 32M "$IMG"


mkfs.ext4 -q -F -d "$ROOT" "$IMG"
echo "created $IMG"

if [ ! -f "$FAT_IMG" ]; then
  echo "=== 构建 FAT 镜像 ($ARCH) ==="
  truncate -s 32M "$FAT_IMG"
  mkfs.fat -F 16 "$FAT_IMG"
  echo "created $FAT_IMG (fresh FAT32)"
else
  echo "FAT image exists, reuse: $FAT_IMG"
fi