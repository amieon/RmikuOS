use alloc::string::String;
use alloc::vec::Vec;

use super::inode::InodeRef;

const MAX_SYMLINKS: usize = 40;

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

/// 跟随路径中的所有 symlink（包括最后一个）。
pub fn lookup_abs_path(path: &str) -> Option<InodeRef> {
    lookup_abs_path_inner(path, true, MAX_SYMLINKS)
}

/// 跟随路径中间组件的 symlink，但最后一个是 symlink 时返回 symlink 本身。
/// lstat/readlink/unlink 这类“看链接而不是看目标”的语义都走这里。
pub fn lookup_abs_path_no_follow(path: &str) -> Option<InodeRef> {
    lookup_abs_path_inner(path, false, MAX_SYMLINKS)
}

pub fn lookup_path_at(cwd: &str, path: &str) -> Option<InodeRef> {
    let abs = normalize_path(cwd, path)?;
    lookup_abs_path(&abs)
}

pub fn lookup_path_at_no_follow(cwd: &str, path: &str) -> Option<InodeRef> {
    let abs = normalize_path(cwd, path)?;
    lookup_abs_path_no_follow(&abs)
}

fn lookup_abs_path_inner(
    path: &str,
    follow_final: bool,
    symlink_budget: usize,
) -> Option<InodeRef> {
    if symlink_budget == 0 {
        log::warn!("[fs] too many symlinks while resolving {}", path);
        return None;
    }

    let path = normalize_path("/", path)?;
    let (mut current, rel) = crate::fs::mount::resolve_mount(&path)?;
    // current_abs 必须是“真实绝对路径”（含挂载点前缀），
    // 否则相对 symlink 会以挂载点内相对路径为基准重解析，跳出所属文件系统。
    let mount_point = crate::fs::mount::mount_point_of(&path)?;
    let mut current_abs = if mount_point == "/" {
        String::from("/")
    } else {
        mount_point
    };
    let parts: Vec<&str> = rel.split('/').filter(|part| !part.is_empty()).collect();

    for (index, part) in parts.iter().enumerate() {
        current = current.lookup(part)?;

        if current_abs != "/" {
            current_abs.push('/');
        }
        current_abs.push_str(part);

        let is_last = index + 1 == parts.len();
        if !current.is_symlink() || (is_last && !follow_final) {
            continue;
        }

        let target = current.readlink()?;
        if target.is_empty() {
            return None;
        }

        let mut substituted = target;
        for rest in &parts[index + 1..] {
            if !substituted.ends_with('/') {
                substituted.push('/');
            }
            substituted.push_str(rest);
        }

        let next = if substituted.starts_with('/') {
            normalize_path("/", &substituted)?
        } else {
            // 相对 symlink 以“包含 symlink 的目录”为基准，而不是进程 cwd。
            normalize_path(parent_dir(&current_abs), &substituted)?
        };

        return lookup_abs_path_inner(&next, follow_final, symlink_budget - 1);
    }

    Some(current)
}

fn parent_dir(path: &str) -> &str {
    let trimmed = path.trim_end_matches('/');
    match trimmed.rfind('/') {
        Some(0) | None => "/",
        Some(index) => &trimmed[..index],
    }
}
