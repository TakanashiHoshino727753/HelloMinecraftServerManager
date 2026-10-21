# ============================================================================
# Kernel / Management（管理层：跨模块横切治理）
# ----------------------------------------------------------------------------
# 这一层不实现任何业务功能，只回答「能不能做、在哪里做、以什么权限做」：
#   modulebus          模块间调用（服务注册表 + 事件总线）
#   diskaccess         磁盘文件访问控制（路径规范化 / 根目录白名单 / 原子写）
#   processprivilege   进程权限管理（提权事实 + 子进程权限策略）
#   resourceallocation 系统资源分配（并发配额 / 端口预约 / 内存策略）
# ============================================================================

add_subdirectory(modulebus)
add_subdirectory(diskaccess)
add_subdirectory(processprivilege)
add_subdirectory(resourceallocation)

# 聚合目标：消费者只需链接 hmcsm_management 即可拿到全部治理服务
add_library(hmcsm_management INTERFACE)
target_link_libraries(hmcsm_management INTERFACE
    hmcsm_modulebus
    hmcsm_diskaccess
    hmcsm_privilege
    hmcsm_resource
)
