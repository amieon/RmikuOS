use super::inode::InodeRef;

pub fn lookup_path(path: &str) -> Option<InodeRef> {
    let path = path.trim();

    if path.is_empty() {
        return None;
    }

    let mut current = crate::fs::initramfs::root_inode();


    //第一版：相对路径也从 root 开始。
    //以后加 cwd 后，再区分 absolute / relative。
    for part in path.split('/') {
        if part.is_empty() || part == "." {
            continue;
        }

        if part == ".." {

            //第一版没有 parent 指针，.. 暂时回 root。
            current = crate::fs::initramfs::root_inode();
            continue;
        }

        current = current.lookup(part)?;
    }

    Some(current)
}