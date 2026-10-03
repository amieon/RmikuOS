//! rsext4 可写 ext4 冒烟测试：mkfs → mount → 建目录/文件 → 写(整块+半块) → sync → umount
//! → 重新 mount(走 journal replay) → lookup 链 → 读回逐字节校验。
//!
//! 位置：kernel/src/test/test_rsext4_rw.rs（test/mod.rs 加 `pub mod test_rsext4_rw;`）
//! 调用：main.rs 里 discover_disks 之后，把一块**可写**盘传进来（见 main.rs 接线说明）。
//!
//! 注意：mkfs 会抹掉这块盘上的所有数据。别传 rootfs 盘。

use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;

use crate::drivers::block::BlockDevice;
use crate::fs::rsext4_adapter::{format_ext4, mount_ext4, RmikuClock};
use rsext4::{FilePermissions, FileName, MutationContext};

pub fn test_rsext4_rw(dev: Arc<dyn BlockDevice>) {
    log::info!("[ext4-rw] ===== rsext4 冒烟测试开始 =====");
    let ctx = MutationContext::new(0, 0, 0, 0o022);

    // ---- 1. mkfs ----
    if let Err(e) = format_ext4(dev.clone()) {
        log::error!("[ext4-rw] FAIL mkfs: {:?}", e);
        return;
    }
    log::info!("[ext4-rw] mkfs OK");

    // ---- 2. mount + 建目录/文件 + 写数据 ----
    let mut fs = match mount_ext4(dev.clone()) {
        Ok(fs) => fs,
        Err(e) => {
            log::error!("[ext4-rw] FAIL mount: {:?}", e);
            return;
        }
    };
    log::info!("[ext4-rw] mount OK, statfs: {:?}", fs.statfs());

    let root = fs.root_inode();

    let dir = match fs.create_directory(ctx, root, FileName::new(b"smoke").unwrap(), FilePermissions::new(0o755).unwrap()) {
        Ok(d) => d,
        Err(e) => {
            log::error!("[ext4-rw] FAIL mkdir /smoke: {:?}", e);
            return;
        }
    };
    log::info!("[ext4-rw] mkdir /smoke OK (inode {})", dir.number.raw());

    let file = match fs.create_regular_file(ctx, dir.number, FileName::new(b"hello.txt").unwrap(), FilePermissions::new(0o644).unwrap()) {
        Ok(f) => f,
        Err(e) => {
            log::error!("[ext4-rw] FAIL create /smoke/hello.txt: {:?}", e);
            return;
        }
    };
    log::info!("[ext4-rw] create /smoke/hello.txt OK (inode {})", file.number.raw());

    // 2 个整块(4096) + 100 字节尾巴：同时覆盖 write_run 直接落盘路径和半块 RMW 缓存路径
    const LEN: usize = 4096 * 2 + 100;
    let payload: Vec<u8> = (0..LEN).map(|i| (i % 251) as u8).collect();
    if let Err(e) = fs.write_inode(file.number, 0, &payload) {
        log::error!("[ext4-rw] FAIL write {} bytes: {:?}", LEN, e);
        return;
    }
    log::info!("[ext4-rw] write {} bytes OK", LEN);

    // ---- 3. sync + 干净卸载 ----
    if let Err(e) = fs.sync() {
        log::error!("[ext4-rw] FAIL sync: {:?}", e);
        return;
    }
    if let Err(e) = fs.unmount() {
        // live orphan 会返回 busy；正常流程到这里不该有
        log::error!("[ext4-rw] FAIL unmount: {:?}", e);
        return;
    }
    log::info!("[ext4-rw] sync + unmount OK");
    drop(fs);

    // ---- 4. 重新 mount（journal 已 checkpoint，走正常挂载路径）----
    let mut fs = match mount_ext4(dev.clone()) {
        Ok(fs) => fs,
        Err(e) => {
            log::error!("[ext4-rw] FAIL remount: {:?}", e);
            return;
        }
    };
    log::info!("[ext4-rw] remount OK");

    // ---- 5. lookup 链 + 读回校验 ----
    let root = fs.root_inode();
    let dir = match fs.lookup_child(root, FileName::new(b"smoke").unwrap()) {
        Ok(Some(d)) => d,
        Ok(None) => {
            log::error!("[ext4-rw] FAIL /smoke 不见了（持久化失败）");
            return;
        }
        Err(e) => {
            log::error!("[ext4-rw] FAIL lookup /smoke: {:?}", e);
            return;
        }
    };
    let file = match fs.lookup_child(dir.number, FileName::new(b"hello.txt").unwrap()) {
        Ok(Some(f)) => f,
        Ok(None) => {
            log::error!("[ext4-rw] FAIL /smoke/hello.txt 不见了");
            return;
        }
        Err(e) => {
            log::error!("[ext4-rw] FAIL lookup hello.txt: {:?}", e);
            return;
        }
    };
    log::info!("[ext4-rw] lookup OK, size = {}", file.size);

    let mut buf = vec![0u8; LEN];
    let n = match fs.read_inode(file.number, 0, &mut buf) {
        Ok(n) => n,
        Err(e) => {
            log::error!("[ext4-rw] FAIL read: {:?}", e);
            return;
        }
    };
    if n != LEN || buf[..] != payload[..] {
        log::error!("[ext4-rw] FAIL 数据校验: 读回 {} 字节（期望 {}）", n, LEN);
        let first_diff = (0..LEN).find(|&i| buf[i] != payload[i]);
        if let Some(i) = first_diff {
            log::error!("[ext4-rw]   首个差异 @ offset {}: 读 {:02x} 期望 {:02x}", i, buf[i], payload[i]);
        }
        return;
    }
    log::info!("[ext4-rw] 读回 {} 字节，逐字节一致", n);

    let _ = fs.unmount();
    log::info!("[ext4-rw] ===== 冒烟测试 PASS =====");
    log::info!("[ext4-rw] 下一步：崩溃一致性测试（写完不 sync 直接杀 QEMU，重启后看 journal replay）");
}

// 让编译器确认 RmikuClock 被 format_ext4 用到（避免未使用告警的说明性引用）
#[allow(dead_code)]
fn _clock_in_use(_: RmikuClock) {}
