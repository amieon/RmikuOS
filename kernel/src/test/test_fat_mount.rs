use alloc::sync::Arc;
use crate::drivers::block::BlockDevice;
use crate::drivers::block::blockio::BlockIo;

pub fn test_fat_mount(fat_dev: Arc<dyn crate::drivers::block::BlockDevice>) {
    // FAT 镜像 32MB / 512 = 65536 扇区
    let io = crate::drivers::block::blockio::BlockIo::new(fat_dev, 65536);

    let fs = match fatfs::FileSystem::new(io, fatfs::FsOptions::new()) {
        Ok(fs) => fs,
        Err(e) => {
            log::error!("[fat] mount failed: {:?}", e);
            return;
        }
    };

    log::info!("[fat] mount OK!");

    let root = fs.root_dir();
    log::info!("[fat] listing root dir:");
    for entry in root.iter() {
        match entry {
            Ok(e) => {
                log::info!("[fat]   {} ({} bytes)", e.file_name(), e.len());
            }
            Err(e) => {
                log::error!("[fat]   iter error: {:?}", e);
                break;
            }
        }
    }
}

pub fn test_fat_cross_sector(fat_dev: Arc<dyn BlockDevice>) {
    use fatfs::{Read, Seek, SeekFrom};
    let mut io = BlockIo::new(fat_dev, 65536);

    // 测试1:从偏移 510 读 10 字节(跨 sector 0/1)
    let mut buf = [0u8; 10];
    io.seek(SeekFrom::Start(510)).unwrap();
    let n = io.read(&mut buf).unwrap();
    log::info!("[fat-cross] read {} bytes from offset 510: {:02x?}", n, buf);
    // sector 0 的最后两字节是 55 aa(偏移 510,511),所以 buf[0..2] 应该是 55 aa

    // 测试2:从偏移 512 读(sector 1 开头,FSInfo)
    let mut buf2 = [0u8; 16];
    io.seek(SeekFrom::Start(512)).unwrap();
    let n2 = io.read(&mut buf2).unwrap();
    log::info!("[fat-cross] read {} bytes from offset 512: {:02x?}", n2, buf2);
    // FSInfo sector 开头应该是 52 52 61 41 ("RRaA")
}

pub fn test_fat_dump_bpb(fat_dev: Arc<dyn BlockDevice>) {
    use fatfs::{Read, Seek, SeekFrom};
    let mut io = BlockIo::new(fat_dev, 65536);

    let mut b = [0u8; 512];
    io.seek(SeekFrom::Start(0)).unwrap();
    io.read(&mut b).unwrap();

    // BPB 关键字段(FAT32)
    let bytes_per_sector = u16::from_le_bytes([b[11], b[12]]);
    let sectors_per_cluster = b[13];
    let reserved_sectors = u16::from_le_bytes([b[14], b[15]]);
    let num_fats = b[16];
    let total_sectors_16 = u16::from_le_bytes([b[19], b[20]]);
    let total_sectors_32 = u32::from_le_bytes([b[32], b[33], b[34], b[35]]);
    let fat_size_32 = u32::from_le_bytes([b[36], b[37], b[38], b[39]]);

    log::info!("[bpb] bytes_per_sector={}", bytes_per_sector);
    log::info!("[bpb] sectors_per_cluster={}", sectors_per_cluster);
    log::info!("[bpb] reserved_sectors={}", reserved_sectors);
    log::info!("[bpb] num_fats={}", num_fats);
    log::info!("[bpb] total_sectors_16={}", total_sectors_16);
    log::info!("[bpb] total_sectors_32={}", total_sectors_32);
    log::info!("[bpb] fat_size_32={}", fat_size_32);
}

pub fn test_fat_table_head(fat_dev: Arc<dyn BlockDevice>) {
    use fatfs::{Read, Seek, SeekFrom};
    let mut io = BlockIo::new(fat_dev, 65536);

    // FAT 表从 reserved_sectors=32 开始,即字节偏移 32*512 = 16384
    let mut b = [0u8; 32];
    io.seek(SeekFrom::Start(32 * 512)).unwrap();
    io.read(&mut b).unwrap();

    log::info!("[fat-table] first 32 bytes at sector 32: {:02x?}", &b);
    // FAT32 期望:
    //   FAT[0] = f8 ff ff 0f  (媒体描述符)
    //   FAT[1] = ff ff ff 0f  (或 ff ff ff ff)
    //   FAT[2] = ff ff ff 0f  (根目录簇的 EOC,通常根在簇 2)
}



pub fn test_sequential_reads(fat_dev: Arc<dyn BlockDevice>) {
    use fatfs::{Read, Seek, SeekFrom};
    let mut io = BlockIo::new(fat_dev, 65536);

    io.seek(SeekFrom::Start(512)).unwrap();   // seek 到 FSInfo

    let mut s1 = [0u8; 4];
    io.read(&mut s1).unwrap();                // 读 4 字节(lead_sig)
    log::info!("[seq] lead = {:02x?} (expect 52 52 61 41)", s1);

    let mut skip = [0u8; 480];
    io.read(&mut skip).unwrap();              // 读 480 字节

    let mut s2 = [0u8; 4];
    io.read(&mut s2).unwrap();                // 读 4 字节(struc_sig)
    log::info!("[seq] struc = {:02x?} (expect 72 72 41 61)", s2);
}

pub fn test_fat_write_persist(fat_dev: Arc<dyn BlockDevice>) {
    let io = BlockIo::new(fat_dev, 65536);

    let fs = match fatfs::FileSystem::new(io, fatfs::FsOptions::new()) {
        Ok(fs) => fs,
        Err(e) => {
            log::error!("[fat] mount failed: {:?}", e);
            return;
        }
    };
    log::info!("[fat] mount OK!");

    let root = fs.root_dir();


    log::info!("[fat] === listing root dir ===");
    let mut found_hello = false;
    for entry in root.iter() {
        match entry {
            Ok(e) => {
                let name = e.file_name();
                log::info!("[fat]   entry: {} ({} bytes)", name, e.len());
                if name == "HELLO.TXT" || name == "hello.txt" {
                    found_hello = true;
                }
            }
            Err(e) => {
                log::error!("[fat]   iter error: {:?}", e);
                break;
            }
        }
    }

    if found_hello {

        log::info!("[fat] *** hello.txt EXISTS (persisted across reboot!) ***");
        use fatfs::Read;
        match root.open_file("hello.txt") {
            Ok(mut file) => {
                let mut buf = [0u8; 128];
                let n = file.read(&mut buf).unwrap_or(0);
                log::info!("[fat] read back {} bytes: {:?}",
                    n, core::str::from_utf8(&buf[..n]).unwrap_or("<invalid utf8>"));
            }
            Err(e) => log::error!("[fat] open_file failed: {:?}", e),
        }
    } else {

        log::info!("[fat] hello.txt not found, creating it (first run)");
        use fatfs::Write;
        match root.create_file("hello.txt") {
            Ok(mut file) => {
                file.write_all(b"hello fat from rmikuos\n").unwrap();
                file.flush().unwrap();
                log::info!("[fat] *** wrote hello.txt, reboot to verify persistence ***");
            }
            Err(e) => log::error!("[fat] create_file failed: {:?}", e),
        }
    }
}