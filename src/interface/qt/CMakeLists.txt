# ============================================================================
# Interface / Qt（C++/QML 前端，按语言分组）
# ----------------------------------------------------------------------------
# 本目录汇集所有「Qt 实现」的前端；非 Qt 语言前端（如 Lua）置于 src/interface/lua/，
# 各自独立目录，互不直接依赖。共 6 个构建目标：GUI/CLI/TLI × Local/Remote。
# ============================================================================

add_subdirectory(gui)
add_subdirectory(cli)
add_subdirectory(tli)
add_subdirectory(remote)
# 远程端依赖 remote 子目录产出的 hmcsm_remoteclient，必须在其后添加
add_subdirectory(cli_remote)
add_subdirectory(tli_remote)
