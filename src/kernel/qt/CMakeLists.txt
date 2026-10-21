# ============================================================================
# Kernel / Qt（C++ 内核实现）
# ----------------------------------------------------------------------------
# 内含三层：management（无依赖）-> execution（依赖 management）
# -> control（依赖前两者，并作为唯一装配点向外暴露）。
# 另含 abi_bind.cpp：把本 Qt 内核包成 contract/hmcsm_abi.h 的 C 函数，供任意前端
# 经 QLibrary 或进程内静态链接使用（见 control/local/abi_bind.cpp）。
# ============================================================================

add_subdirectory(management)
add_subdirectory(execution)
add_subdirectory(control)
