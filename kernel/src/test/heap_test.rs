pub fn heap_test() {
    use alloc::boxed::Box;
    use alloc::string::String;
    use alloc::vec::Vec;

    log::info!("[heap] heap test start");

    let b = Box::new(0x2333usize);
    assert_eq!(*b, 0x2333);
    log::info!("[heap] Box test passed: {:#x}", *b);

    let mut v = Vec::new();
    for i in 0..128 {
        v.push(i);
    }

    assert_eq!(v.len(), 128);
    assert_eq!(v[0], 0);
    assert_eq!(v[127], 127);
    log::info!("[heap] Vec test passed: len={}", v.len());

    let mut s = String::from("RmikuOS");
    s.push_str(" heap OK");
    log::info!("[heap] String test passed: {}", s);

    drop(v);
    drop(s);
    drop(b);

    let mut boxes = Vec::new();
    for i in 0..1024 {
        boxes.push(Box::new(i));
    }

    for i in 0..1024 {
        assert_eq!(*boxes[i], i);
    }

    log::info!("[heap] many Box test passed");
    log::info!("[heap] heap test passed");
}