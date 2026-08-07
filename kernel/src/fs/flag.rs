pub const O_RDONLY: usize =   0;
pub const O_WRONLY : usize =  1;
pub const O_RDWR  : usize =   2;
pub const O_CREAT : usize =   0x40;
pub const O_TRUNC : usize =   0x200;
pub const O_APPEND : usize =  0x400 ;
pub const O_ACCMODE: usize = 0x3;
/// 仅用于 exec: 打开时不检查 R/W, 由 exec 自行检查 X 权限。
/// 注意: 不能与 O_NONBLOCK(0x800) 冲突, 这里用 0x1000。
pub const O_EXEC: usize = 0x1000;