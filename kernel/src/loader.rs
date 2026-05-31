mod generated {
    include!("loader/generated.rs");
}

pub fn num_apps() -> usize {
    generated::APP_NUM
}

pub fn get_app_data(app_id: usize) -> &'static [u8] {
    generated::get_app_data(app_id)
}

pub fn get_app_name(app_id: usize) -> &'static str {
    generated::get_app_name(app_id)
}

fn strip_numeric_prefix(name: &str) -> &str {
    let bytes = name.as_bytes();

    let mut i = 0usize;
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }

    if i > 0 && i < bytes.len() && bytes[i] == b'_' {
        &name[i + 1..]
    } else {
        name
    }
}

fn basename(path: &str) -> &str {
    match path.rsplit('/').next() {
        Some(name) => name,
        None => path,
    }
}

pub fn find_app(name: &str) -> Option<usize> {
    let name = name.trim_matches('\0').trim();
    let name = basename(name);

    for id in 0..num_apps() {
        let app_name = get_app_name(id);
        let short_name = strip_numeric_prefix(app_name);

        if name == app_name || name == short_name {
            return Some(id);
        }
    }

    None
}