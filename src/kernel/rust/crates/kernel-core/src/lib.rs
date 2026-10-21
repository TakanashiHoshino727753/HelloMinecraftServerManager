// ============================================================================
// HMCSM Rust 内核：契约层 C ABI 实现（骨架）
// ----------------------------------------------------------------------------
// 本 crate 导出的 C 函数名与 src/kernel/contract/hmcsm_abi.h 完全一致，因此 Qt 前端的
// HmcsmLoader("hmcsm_kernel_rust") 能直接解析到这些符号（与 Qt 后端的 hmcsm_kernel_qt
// 同形状）。跨语言边界仅用 C 类型与「不透明句柄」，字符串统一 UTF-8（*const c_char）。
//
// 当前为骨架：仅保证 ABI 形状正确、可编译、可被前端加载；具体业务逻辑待后续填充。
// ============================================================================

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

/// 不透明内核句柄（对应 C 侧 `struct HmcsmKernel`）。内部状态后续填充。
#[repr(C)]
pub struct HmcsmKernel {
    // 占位字段：未来承载 Rust 内核状态。
    _private: (),
}

unsafe fn to_str<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok()
}

/// 创建内核实例。cfg_path 可为空。
#[no_mangle]
pub extern "C" fn hmcsm_kernel_create(cfg_path: *const c_char) -> *mut HmcsmKernel {
    if let Some(p) = unsafe { to_str(cfg_path) } {
        let _ = p; // 暂未使用；未来解析配置文件。
    }
    Box::into_raw(Box::new(HmcsmKernel { _private: () }))
}

/// 释放内核实例。
#[no_mangle]
pub extern "C" fn hmcsm_kernel_free(kernel: *mut HmcsmKernel) {
    if !kernel.is_null() {
        unsafe {
            drop(Box::from_raw(kernel));
        }
    }
}

/// 启动指定服务器。
#[no_mangle]
pub extern "C" fn hmcsm_kernel_start(
    _kernel: *mut HmcsmKernel,
    _server: *const c_char,
    _path: *const c_char,
) {
    // TODO: 调用 Rust 内核的开始逻辑。
}

/// 停止指定服务器。
#[no_mangle]
pub extern "C" fn hmcsm_kernel_stop(_kernel: *mut HmcsmKernel, _path: *const c_char) {
    // TODO: 调用 Rust 内核的停止逻辑。
}

/// 向控制台发送一条指令。
#[no_mangle]
pub extern "C" fn hmcsm_kernel_send(
    _kernel: *mut HmcsmKernel,
    _path: *const c_char,
    _cmd: *const c_char,
) {
    // TODO: 调用 Rust 内核的指令下发逻辑。
}
