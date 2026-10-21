# ============================================================================
# Shared / Loader（前端侧后端加载器，基于 QLibrary）
# ----------------------------------------------------------------------------
# 提供 HmcsmLoader：前端据此在运行期选择后端（进程内 Qt 后端 / 动态加载的 Rust 后端），
# 解析 contract/hmcsm_abi.h 声明的 C 函数。自身不依赖任何具体内核实现。
# ============================================================================

set(CMAKE_AUTOMOC ON)

add_library(hmcsm_loader STATIC
    hmcsm_loader.h
    hmcsm_loader.cpp
)

target_link_libraries(hmcsm_loader PUBLIC
    Qt6::Core
    hmcsm_abi
)
