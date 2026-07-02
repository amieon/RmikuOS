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

# 先复制用户自定义 rootfs 模板
if [ -d "$OVERLAY" ]; then
  echo "copy rootfs overlay: $OVERLAY -> $ROOT"
  mkdir -p "$ROOT"
  cp -a "$OVERLAY"/. "$ROOT"/
else
  echo "no $OVERLAY, create minimal rootfs"
  mkdir -p "$ROOT"
fi

# 确保基础目录存在
mkdir -p "$ROOT/programs" "$ROOT/bin" "$ROOT/etc" "$ROOT/home" "$ROOT/tmp" "$ROOT/dev" "$ROOT/proc" "$ROOT/tests" "$ROOT/fat" "$ROOT/gcn"

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

# 把编译出来的测试程序放进 /tests
for f in user/build/${ARCH}/tests/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"          
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"  
  cp "$f" "$ROOT/tests/$clean"
done

# 把 Rust 单文件/工作空间编译出来的程序放进 /programs
for f in user/build/${ARCH}/programs/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"
  cp "$f" "$ROOT/programs/$base"
done

#C 项目（单入口扁平化，多入口建子目录）
for proj_dir in user/build/${ARCH}/c/*; do
  [ -d "$proj_dir" ] || continue
  proj_name="$(basename "$proj_dir")"
  elf_count=$(ls "$proj_dir"/*.elf 2>/dev/null | wc -l)
  if [ "$elf_count" -eq 1 ]; then
    # 单入口：直接放 /programs/，和 Rust 一致
    cp "$proj_dir"/*.elf "$ROOT/programs/$proj_name"
    echo "  [c] $proj_name -> /programs/$proj_name"
  else
    # 多入口：放子目录
    mkdir -p "$ROOT/programs/$proj_name"
    for f in "$proj_dir"/*.elf; do
      [ -e "$f" ] || continue
      base="$(basename "$f" .elf)"
      cp "$f" "$ROOT/programs/$proj_name/$base"
    done
    echo "  [c multi] $proj_name -> /programs/$proj_name/"
  fi
done

# C++ 项目（单入口扁平化，多入口建子目录）
for proj_dir in user/build/${ARCH}/cpp/*; do
  [ -d "$proj_dir" ] || continue
  proj_name="$(basename "$proj_dir")"
  elf_count=$(ls "$proj_dir"/*.elf 2>/dev/null | wc -l)
  if [ "$elf_count" -eq 1 ]; then
    # 单入口：直接放 /programs/，和 Rust 一致
    cp "$proj_dir"/*.elf "$ROOT/programs/$proj_name"
    echo "  [cpp] $proj_name -> /programs/$proj_name"
  else
    # 多入口：放子目录
    mkdir -p "$ROOT/programs/$proj_name"
    for f in "$proj_dir"/*.elf; do
      [ -e "$f" ] || continue
      base="$(basename "$f" .elf)"
      cp "$f" "$ROOT/programs/$proj_name/$base"
    done
    echo "  [cpp multi] $proj_name -> /programs/$proj_name/"
  fi
done

# 新增：GCN（特殊目录 /gcn/）
if [ -d "user/build/${ARCH}/gcn" ]; then
  for f in user/build/${ARCH}/gcn/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/gcn/$base"
  done
  echo "  [gcn] -> /gcn/"
fi

# 简单展示 rootfs 内容
echo "rootfs content:"
find "$ROOT" -maxdepth 3 -print | sort

# 构建镜像
rm -f "$IMG"
truncate -s 32M "$IMG"

mkfs.ext4 -q -F -d "$ROOT" "$IMG"
echo "created $IMG"

if [ ! -f "$FAT_IMG" ]; then
  echo "=== 构建 FAT 镜像 ($ARCH) ==="
  truncate -s 32M "$FAT_IMG"
  mkfs.fat -F 16 "$FAT_IMG"
  echo "created $FAT_IMG (fresh FAT16)"
else
  echo "FAT image exists, reuse: $FAT_IMG"
fi