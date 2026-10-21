# ============================================================================
# Interface（接口层：按语言分组的多前端）
# ----------------------------------------------------------------------------
#   qt/        Qt(C++/QML) 前端集合：gui / cli / tli / remote / cli_remote / tli_remote
#              （GUI/CLI/TLI × Local/Remote，共 6 个构建目标）
#   lua/       Lua 前端（脚本，经 WebUI REST API 对接任意后端），不纳入 CMake 构建
#
# 各前端只做「呈现 + 输入」，一律经契约（LocalConnection / WebUI REST API）访问内核，
# 不直接触碰执行模块。前端与后端（Qt/Rust）通过 src/kernel/contract 的 C ABI 解耦。
# ============================================================================

# Qt(C++/QML) 前端
add_subdirectory(qt)

# 其它语言前端（经 WebUI REST API），仅占位，不产出 CMake 目标
add_subdirectory(lua)
