# ============================================================================
# Kernel / Execution（执行层：真正做事的业务模块）
# ----------------------------------------------------------------------------
# 依赖方向：execution -> management，且各执行模块之间尽量不要互相引用；
# 必须互相调用时经 ModuleBus 按名解析，不要直接 #include 对方内部头文件。
# ============================================================================

# download 内含 HttpClient（Link Acquisition 的网络原语）与 DownloadListModel，
# 需先于其它模块定义，故放在最前。
add_subdirectory(download)
add_subdirectory(java)
add_subdirectory(server)
add_subdirectory(settings)
add_subdirectory(notify)
add_subdirectory(monitor)
add_subdirectory(ops)
add_subdirectory(proxy)
add_subdirectory(database)
add_subdirectory(ai)
add_subdirectory(interfaces)
