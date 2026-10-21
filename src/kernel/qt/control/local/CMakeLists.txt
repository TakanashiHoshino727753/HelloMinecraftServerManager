# ============================================================================
# Kernel / Control / Local（本地直连 + 内核组合根）
# ----------------------------------------------------------------------------
# kernel.h/.cpp          组合根：按依赖顺序构造三层全部服务并完成跨模块接线
# localconnection.h/.cpp 本地直连：把内核服务按名暴露给接口层（QML 上下文属性 / CLI / TLI）
# ============================================================================

add_library(hmcsm_kernel STATIC
    kernel.h
    kernel.cpp
    localconnection.h
    localconnection.cpp
    abi_bind.cpp
)

target_link_libraries(hmcsm_kernel PUBLIC
    Qt6::Core
    Qt6::Qml
    hmcsm_abi

    # Management
    hmcsm_management

    # Execution
    hmcsm_download
    hmcsm_java
    hmcsm_server
    hmcsm_server
    hmcsm_settings
    hmcsm_notify
    hmcsm_monitor
    hmcsm_ops
    hmcsm_proxy
    hmcsm_database
    hmcsm_ai
    hmcsm_interfaces

    # Control / Remote
    hmcsm_webui
    hmcsm_remotecontrol
    hmcsm_bot

    # shared 模块：统一日志（hmcsm_logging 的 include 目录随链接带出）与外观模型
    hmcsm_logging
    hmcsm_appearance
)

target_include_directories(hmcsm_kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/shared
)
