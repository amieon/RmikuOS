pub trait BlockDevice: Send + Sync {
    fn block_size(&self) -> usize;

    fn num_blocks(&self) -> usize;

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize;

    fn write_block(&self, block_id: usize, buf: &[u8]) -> isize;

    /// 一次 `read_blocks` 最多能读多少块。
    ///
    /// 默认 1 = 不支持批量（调用方按单块发）。virtio 这类有固定大小 DMA
    /// 缓冲的设备必须覆写成真实容量，否则批量接口会发出超过缓冲的请求。
    fn max_transfer_blocks(&self) -> usize {
        1
    }

    /// 一次读多个连续块。成功返回字节数（= count * block_size()），失败返回 -1。
    ///
    /// 默认实现就是循环调 read_block —— 对 RamDisk 这种"读一块 = 一次 memcpy"
    /// 的设备，拆开和合并没有区别，没必要为它写批量版本。真正值得覆写的是
    /// 要过 ring / 中断的设备（virtio），那里每次请求都是一次 vCPU 退出。
    ///
    /// 没有"部分成功"：返回 -1 时 buf 的内容无意义，调用方不许当有效数据用。
    fn read_blocks(&self, start: usize, count: usize, buf: &mut [u8]) -> isize {
        let bs = self.block_size();

        if count == 0 {
            return 0;
        }

        let bytes = match count.checked_mul(bs) {
            Some(v) => v,
            None => return -1,
        };

        if bs == 0 || buf.len() < bytes {
            return -1;
        }

        for i in 0..count {
            let ret = self.read_block(start + i, &mut buf[i * bs..(i + 1) * bs]);

            if ret != bs as isize {
                return -1;
            }
        }

        bytes as isize
    }

    /// 把设备上所有写缓存刷盘。默认无操作(直通设备不需要)。
    /// 带写缓存的硬件设备应重写此方法,发出真正的 flush 命令。
    fn flush(&self) -> isize {
        0
    }
}