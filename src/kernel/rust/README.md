# HMCSM Rust 内核

Rust 实现的 **内核后端**（与 `src/kernel/qt/` 平级，按语言分组），是「Qt + Rust 双版本内核」
规划中的 Rust 一侧。它**不纳入 CMake 构建**，而是作为独立的 **Cargo workspace** 单独编译。

## 构建

```bash
cd src/kernel/rust
cargo build --release      # 产出 target/release/libhmcsm_kernel.dll（cdylib）
```

产物 `libhmcsm_kernel` 导出的 C 函数与 `src/kernel/contract/hmcsm_abi.h` 完全一致，因此
Qt 前端的 `HmcsmLoader` 可用 `load("hmcsm_kernel_rust")` 在运行期加载它——无需改动任何前端代码。

## 与 Qt 内核的关系

- 两者共用同一份契约（`hmcsm_abi.h`）+ WebUI REST API，前端无感知；
- Qt 前端 ↔ Qt 后端可进程内直连（Local）；Qt 前端 ↔ Rust 后端经 `QLibrary` 加载本动态库；
- 非 C++ 前端（如 Lua）统一走 REST，与本 crate 无直接耦合。
