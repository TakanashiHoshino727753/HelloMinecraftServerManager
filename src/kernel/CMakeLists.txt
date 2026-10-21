# ============================================================================
# Kernel（内核）
# ----------------------------------------------------------------------------
# 按依赖顺序构建三层：management（无依赖）-> execution（依赖 management）
# -> control（依赖前两者，并作为唯一装配点向外暴露）。
# ============================================================================

# 契约层（语言无关 C ABI，前后端唯一耦合点）
add_subdirectory(contract)

# Qt(C++) 内核实现
add_subdirectory(qt)

# Rust 内核实现（Cargo workspace，独立于本 CMake 构建，见 src/kernel/rust/README.md）
# 不纳入 add_subdirectory：由 cargo 单独编译为 libhmcsm_kernel.{dll,so}，供前端 QLibrary 加载。
