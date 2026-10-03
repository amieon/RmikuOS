use alloc::sync::Arc;

use rsext4::{
    BlockIo, Clock, DeviceCapabilities, DeviceGeometry, EntropySource, Ext4, Ext4Error,
    Ext4Result, Ext4Timestamp, MountOptions, MountServices, MountedServices, NoopObserver, SectorId,
};

use crate::drivers::block::BlockDevice;



pub struct RmikuBlockIo {
    inner: Arc<dyn BlockDevice>,
}

impl RmikuBlockIo {
    pub fn new(inner: Arc<dyn BlockDevice>) -> Self {
        assert_eq!(inner.block_size(), 512, "rsext4 适配层假设 512 字节扇区");
        Self { inner }
    }
}

fn io_errno(errno: isize) -> Ext4Error {
    // ErrorContext 是 Copy 枚举（只能装 'static str），动态 errno 记日志。
    log::warn!("[ext4-rw] block io error: errno={}", errno);
    Ext4Error::io().with_operation("rmiku-blk-io")
}

impl BlockIo for RmikuBlockIo {
    fn write(&mut self, buffer: &[u8], sector: SectorId, count: u32) -> Ext4Result<()> {
        let base = sector.raw() as usize;
        for i in 0..count as usize {
            let chunk = &buffer[i * 512..(i + 1) * 512];
            // RmikuOS 约定：成功返回写入字节数(512)，失败返回负数（同 blockio.rs）
            let rc = self.inner.write_block(base + i, chunk);
            if rc != 512 {
                return Err(io_errno(rc));
            }
        }
        Ok(())
    }

    fn read(&mut self, buffer: &mut [u8], sector: SectorId, count: u32) -> Ext4Result<()> {
        let base = sector.raw() as usize;
        for i in 0..count as usize {
            let chunk = &mut buffer[i * 512..(i + 1) * 512];
            let rc = self.inner.read_block(base + i, chunk);
            if rc != 512 {
                return Err(io_errno(rc));
            }
        }
        Ok(())
    }

    fn geometry(&self) -> DeviceGeometry {
        DeviceGeometry::new(512, self.inner.num_blocks() as u64)
    }

    fn capabilities(&self) -> DeviceCapabilities {
        DeviceCapabilities {
            read_only: false,
            flush: true,
            barrier: false,
            fua: false,
            discard: false,
        }
    }

    fn flush(&mut self) -> Ext4Result<()> {
        // trait 默认实现返回 0=成功；设备覆盖时也应遵守（负数=错）
        let rc = self.inner.flush();
        if rc < 0 {
            return Err(io_errno(rc));
        }
        Ok(())
    }
}

pub struct RmikuClock;

impl Clock for RmikuClock {
    fn now(&self) -> Ext4Result<Ext4Timestamp> {
        // TODO: 换成 RmikuOS 的 RTC 读取
        Ok(Ext4Timestamp::new(rmiku_rtc_unix_seconds(), 0))
    }
}

// 占位：接内核 RTC 后删掉
fn rmiku_rtc_unix_seconds() -> i64 {
    0
}

/// 熵源：接内核 RNG。
pub struct RmikuEntropy;

impl EntropySource for RmikuEntropy {
    fn fill_bytes(&mut self, output: &mut [u8]) -> Ext4Result<()> {
        // TODO: 换成内核 RNG；返回 Err 则 rsext4 报 unsupported_capability("runtime:entropy")
        output.fill(0);
        Ok(())
    }
}


pub type Ext4Mount = Ext4<RmikuBlockIo, MountedServices<RmikuEntropy, NoopObserver>>;

/// 数据盘固定卷标（约定式身份）：discover_disks 按 s_volume_name 识别角色。
/// 与 mkfs_ext4.sh 里 rootfs 的 -L RMikuOS-ROOT 对应。
pub const DATA_VOLUME_LABEL: &[u8] = b"RMikuOS-DATA";

/// 在块设备上格式化 ext4（会抹掉盘上所有数据！）
/// 数据盘首次挂载流程：mount 失败 → format_ext4 → 重新 mount。
pub fn format_ext4(dev: Arc<dyn BlockDevice>) -> Ext4Result<()> {
    let io = RmikuBlockIo::new(dev);
    // 写入约定卷标（需 vendored rsext4 的 MkfsOptions.volume_label 补丁）
    let mut opts = rsext4::MkfsOptions::default();
    let n = DATA_VOLUME_LABEL.len().min(16);
    opts.volume_label[..n].copy_from_slice(&DATA_VOLUME_LABEL[..n]);
    // format 吃掉设备再还回来（mkfs 内部临时关 journal，结束前恢复）
    rsext4::format(io, RmikuClock, opts).map(|_| ())
}

pub fn mount_ext4(dev: Arc<dyn BlockDevice>) -> Ext4Result<Ext4Mount> {
    let io = RmikuBlockIo::new(dev);
    let services = MountServices::new(RmikuClock, RmikuEntropy, NoopObserver);
    // read_write() = { readonly: false, replay_journal: true, block_validity: true }
    // 若要启用 MMP：services.with_mmp(RmikuDelay, MmpIdentity{...}) 再传进来
    Ext4::mount(io, services, MountOptions::read_write())
}

// ============================================================================
// 4. VFS 映射清单（接 syscall 时照此）
// ============================================================================
// - fd 表存 (InodeNumber, offset)，不存 path；lookup_child 是路径解析入口
// - 读写用 Ext4::read_inode / write_inode（inode + offset）
// - fsync/fdatasync -> Ext4::sync()（全量，无 per-file sync，文档写清楚）
// - 内核 writeback 线程每 1-5s 调 sync() 压缩崩溃窗口
// - umount 有 live orphan 时返回 busy → 接 VFS 的 EBUSY
// - 锁模型：VFS 侧 Mutex<Ext4Mount> 一把锁（rsext4 核心零内部同步）
// - 镜像统一 mkfs.ext4 -b 4096
