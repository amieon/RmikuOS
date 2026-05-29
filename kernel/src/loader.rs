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