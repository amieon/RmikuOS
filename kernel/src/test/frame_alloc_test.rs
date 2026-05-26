pub fn frame_alloc_test() {
    let a = crate::mm::alloc_frame().expect("alloc a failed");
    let b = crate::mm::alloc_frame().expect("alloc b failed");
    let c = crate::mm::alloc_frame().expect("alloc c failed");

    log::info!("[mm] alloc frames: {:?}, {:?}, {:?}", a, b, c);

    crate::mm::dealloc_frame(b);

    let d = crate::mm::alloc_frame().expect("alloc d failed");

    assert_eq!(b, d);

    log::info!("[mm] frame allocator recycle test passed");
}