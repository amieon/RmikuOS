use alloc::sync::Arc;
use alloc::string::String;
use alloc::vec::Vec;
use super::inode::InodeRef;
use crate::sync::spin::Mutex;

pub trait FileSystem: Send + Sync {
    fn root_inode(self: Arc<Self>) -> InodeRef;
}

pub struct Mount {
    mount_point : String,
    /// 该挂载点的根在**底层 fs 内**的路径(相对 fs 根, 无前导 '/')。
    /// 普通挂载是 ""(fs 根即挂载点根); bind 挂载要记源路径, 例如
    /// `/home` ← bind 自 `/data/home` 时是 "home"。
    /// rename 要走目标父目录的父子链, 只有这个字段能把 VFS 路径翻译回 fs 内路径。
    inner_base  : String,
    root : InodeRef,
}

static MOUNTS: Mutex<Vec<Mount>> = Mutex::new(Vec::new());


pub fn mount(mount_point: &str, fs: Arc<dyn FileSystem>) {
    let root = fs.clone().root_inode();
    let mut mounts = MOUNTS.lock();
    mounts.push(Mount {
        mount_point: String::from(mount_point),
        inner_base: String::new(),
        root,
    });
}


/// 把已存在的 inode 绑到另一个路径上(bind mount)。
/// `inner_base` = 该 inode 在底层 fs 内的路径(相对 fs 根), 由调用方给出 ——
/// 例如 `/home` 绑 `/data/home`, 底层 fs 挂在 `/data`, 则 inner_base = "home"。
pub fn mount_bind(mount_point: &str, inode: InodeRef, inner_base: &str) {
    let mut mounts = MOUNTS.lock();
    mounts.push(Mount {
        mount_point: String::from(mount_point),
        inner_base: String::from(inner_base.trim_matches('/')),
        root: inode,
    });
}


pub fn resolve_mount(abs_path: &str) -> Option<(InodeRef, String)> {
    let mounts = MOUNTS.lock();

    let mut best: Option<&Mount> = None;

    for m in mounts.iter() {
        if path_under(abs_path, &m.mount_point) {
            
            match best {
                None => best = Some(m),
                Some(b) if m.mount_point.len() > b.mount_point.len() => best = Some(m),
                _ => {}
            }
        }
    }

    let m = best?;
    let root = m.root.clone();
    let rel = relative_path(abs_path, &m.mount_point);
    Some((root, rel))
}

/// 返回 abs_path 所属挂载点的挂载路径(如 "/tmp"、"/")。用于跨设备检测。
/// 注意: 这与 `resolve_mount` 不同——后者第二个返回值是挂载点之内的相对路径,
/// 而本函数返回的是挂载点本身。rename 的 EXDEV 判断必须用本函数。
pub fn mount_point_of(abs_path: &str) -> Option<String> {
    best_mount_of(abs_path).map(|(_, mp, _)| mp)
}

/// 返回 (挂载点, 挂载点根在底层 fs 内的路径)。
/// rename 要把 VFS 绝对路径翻译成"相对 fs 根"的路径才能让各 fs 走父子链;
/// 光剥挂载点前缀不够 —— bind 出来的挂载点(如 /home ← /data/home)会少一段。
pub fn mount_info_of(abs_path: &str) -> Option<(String, String)> {
    best_mount_of(abs_path).map(|(_, mp, inner)| (mp, inner))
}

/// (root inode, 挂载点, inner_base) —— 最长挂载点前缀匹配(嵌套挂载取更深的)。
/// 都在锁内 clone 出来, 避免把 MutexGuard 的借出带出作用域。
fn best_mount_of(abs_path: &str) -> Option<(InodeRef, String, String)> {
    let mounts = MOUNTS.lock();
    let mut best: Option<&Mount> = None;
    for m in mounts.iter() {
        if path_under(abs_path, &m.mount_point) {
            match best {
                None => best = Some(m),
                Some(b) if m.mount_point.len() > b.mount_point.len() => best = Some(m),
                _ => {}
            }
        }
    }
    best.map(|m| (m.root.clone(), m.mount_point.clone(), m.inner_base.clone()))
}

fn path_under(abs_path: &str, mount_point: &str) -> bool {
    if mount_point == "/" {
        return true;  
    }
    if abs_path == mount_point {
        return true; 
    }
    abs_path.starts_with(mount_point)
        && abs_path.as_bytes().get(mount_point.len()) == Some(&b'/')
}


fn relative_path(abs_path: &str, mount_point: &str) -> String {
    if mount_point == "/" {
        return String::from(abs_path.trim_start_matches('/'));
    }
    let rest = &abs_path[mount_point.len()..];
    String::from(rest.trim_start_matches('/'))
}