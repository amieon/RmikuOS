use alloc::string::String;
use alloc::vec::Vec;

use super::inode::InodeRef;

pub fn normalize_path(cwd: &str, path: &str) -> Option<String> {
    let path = path.trim();

    if path.is_empty() {
        return None;
    }

    let mut parts: Vec<&str> = Vec::new();

    /*
     * 绝对路径从 / 开始。
     * 相对路径从 cwd 开始。
     */
    if !path.starts_with('/') {
        for part in cwd.split('/') {
            if part.is_empty() || part == "." {
                continue;
            }
            if part == ".." {
                parts.pop();
            } else {
                parts.push(part);
            }
        }
    }

    for part in path.split('/') {
        if part.is_empty() || part == "." {
            continue;
        }

        if part == ".." {
            parts.pop();
        } else {
            parts.push(part);
        }
    }

    let mut out = String::new();
    out.push('/');

    for (i, part) in parts.iter().enumerate() {
        if i > 0 {
            out.push('/');
        }
        out.push_str(part);
    }

    Some(out)
}

pub fn lookup_abs_path(path: &str) -> Option<InodeRef> {
    let path = normalize_path("/", path)?;

    let mut current = crate::fs::initramfs::root_inode();

    if path == "/" {
        return Some(current);
    }

    for part in path.split('/') {
        if part.is_empty() {
            continue;
        }

        current = current.lookup(part)?;
    }

    Some(current)
}

pub fn lookup_path_at(cwd: &str, path: &str) -> Option<InodeRef> {
    let abs = normalize_path(cwd, path)?;
    lookup_abs_path(&abs)
}