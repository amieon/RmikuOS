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
    fs : Arc<dyn FileSystem>,
}

static MOUNTS: Mutex<Vec<Mount>> = Mutex::new(Vec::new());


pub fn mount(mount_point: &str, fs: Arc<dyn FileSystem>) {
    let mut mounts = MOUNTS.lock();
    mounts.push(Mount {
        mount_point: String::from(mount_point),
        fs,
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
    let root = m.fs.clone().root_inode();
    let rel = relative_path(abs_path, &m.mount_point);
    Some((root, rel))
}

/// 返回 abs_path 所属挂载点的挂载路径(如 "/tmp"、"/")。用于跨设备检测。
/// 注意: 这与 `resolve_mount` 不同——后者第二个返回值是挂载点之内的相对路径,
/// 而本函数返回的是挂载点本身。rename 的 EXDEV 判断必须用本函数。
pub fn mount_point_of(abs_path: &str) -> Option<String> {
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
    best.map(|m| m.mount_point.clone())
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