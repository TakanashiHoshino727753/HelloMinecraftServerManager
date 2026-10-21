#pragma once

// ============================================================================
// HMCSM 内核 C ABI（语言无关契约）
// ----------------------------------------------------------------------------
// 这是前后端之间唯一的耦合点：无论后端是 Qt(C++) 还是 Rust，无论前端是 Qt 还是
// Lua/其它语言，都通过本头文件声明的一小组 C 函数交互。前端只持有不透明句柄
// HmcsmKernel*，永远看不到后端的具体类型布局；后端则实现这些 C 函数。
//
// 设计要点：
//   - 仅用 C 类型与「不透明句柄」，可被 C++ / Rust(cdylib) / 任意语言 FFI 直接调用；
//   - 谁分配谁释放：HmcsmKernel* 由 hmcsm_kernel_create 分配、hmcsm_kernel_free 释放；
//   - 跨语言字符串统一用 const char*（UTF-8），前端不负责释放后端返回的字符串；
//   - 同语言前端（Qt↔Qt）可把本 ABI 静态链入进程内（Local 模式）；异语言前端
//     （如 Lua）经 WebUI REST API 对接，本 ABI 是 Rust 后端导出供 Qt 前端 QLibrary
//     加载的契约形状。
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// 不透明内核句柄：前端只知其是指针，不知内部结构。
typedef struct HmcsmKernel HmcsmKernel;

// 创建一个内核实例。cfg_path 为可选的配置文件路径（UTF-8），可为空串。
// 返回的句柄须由 hmcsm_kernel_free 释放。失败时返回 nullptr。
HmcsmKernel* hmcsm_kernel_create(const char* cfg_path);

// 释放一个内核实例及其全部资源。
void hmcsm_kernel_free(HmcsmKernel* kernel);

// 启动指定服务器。server 为服务器名，path 为服务器目录路径（UTF-8）。
void hmcsm_kernel_start(HmcsmKernel* kernel, const char* server, const char* path);

// 停止指定服务器（path 标识）。
void hmcsm_kernel_stop(HmcsmKernel* kernel, const char* path);

// 向指定服务器控制台发送一条指令（UTF-8）。
void hmcsm_kernel_send(HmcsmKernel* kernel, const char* path, const char* cmd);

#ifdef __cplusplus
}
#endif
