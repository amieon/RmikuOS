extern crate alloc;

use alloc::sync::Arc;
use alloc::vec;

use crate::drivers::block::BlockDevice;

/// 验证 `read_blocks` 的字节正确性。
///
/// 批量读最容易错的地方不是"能不能读到"，而是**偏移和长度算错但看不出来**：
/// desc.len 写小了会截断，sector 算错会整体错位，DMA 数据区没对齐会把上一轮
/// 的残留当成数据。所以这里不比"读到的内容有意义"，只比**批量读的第 i 块
/// 必须逐字节等于单独读第 i 块**——错位、截断、串块全都会立刻暴露。
pub fn test_batch_read(dev: Arc<dyn BlockDevice>) {
    let max = dev.max_transfer_blocks();

    log::info!(
        "[batch-read] num_blocks={} max_transfer_blocks={}",
        dev.num_blocks(),
        max,
    );

    if max <= 1 {
        log::warn!("[batch-read] device has no batch support, skip");
        return;
    }

    let count = core::cmp::min(max, 128);

    // 1. 单块读 ext4 superblock，确认设备本身是好的
    let mut single = [0u8; 512];
    assert_eq!(
        dev.read_block(2, &mut single),
        512,
        "[batch-read] single read failed",
    );
    let magic = u16::from_le_bytes([single[56], single[57]]);
    assert_eq!(magic, 0xef53, "[batch-read] bad ext4 magic: {:#x}", magic);

    // 2. 批量读，起点 0
    let mut batch = vec![0u8; count * 512];
    let ret = dev.read_blocks(0, count, &mut batch);
    assert_eq!(
        ret,
        (count * 512) as isize,
        "[batch-read] batch read returned {} (expect {})",
        ret,
        count * 512,
    );

    // 3. 抽样比对：首 / 中 / 尾。全比会额外发 count 次单块请求，
    //    抽样同样能抓到错位和截断。
    let probes = [0usize, count / 2, count - 1];

    for &i in probes.iter() {
        let mut one = [0u8; 512];
        assert_eq!(
            dev.read_block(i, &mut one),
            512,
            "[batch-read] single read of block {} failed",
            i,
        );

        let lo = i * 512;
        let hi = lo + 512;

        assert!(
            one[..] == batch[lo..hi],
            "[batch-read] block {} differs between single and batch read",
            i,
        );
    }

    // 4. 非零起点：这一条才是真正在验证 sector 字段。
    //    起点写错（比如忘了加 start）会读到错位的数据，而起点 0 的测试看不出来。
    let start = 5usize;
    let span = core::cmp::min(count, 16);

    let mut shifted = vec![0u8; span * 512];
    let ret = dev.read_blocks(start, span, &mut shifted);
    assert_eq!(ret, (span * 512) as isize, "[batch-read] shifted read failed");

    for &i in [0usize, span - 1].iter() {
        let mut one = [0u8; 512];
        assert_eq!(dev.read_block(start + i, &mut one), 512);

        let lo = i * 512;
        let hi = lo + 512;

        assert!(
            one[..] == shifted[lo..hi],
            "[batch-read] block {} at start {} differs",
            i,
            start,
        );
    }

    log::info!("[batch-read] ok: {} sectors per request", count);
}
