# ============================================================================
# Interface / GUI（桌面图形界面，主程序）
# ----------------------------------------------------------------------------
# 本目录只放「桌面端特有的东西」：入口 main.cpp、窗口/托盘控制器 AppController、
# QML 界面（qml/）与图标资源。业务对象全部由 src/kernel 提供。
# ============================================================================

# Theme / I18n 作为单例在 QML 中通过 import MinecraftServerManager 1.0 访问。
# 必须在 qt_add_qml_module 之前设置，否则属性在模块处理文件时尚未生效，
# 导致 Theme 被注册成普通类型（qmldir 无 singleton 前缀），运行时取不到值。
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/I18n.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# PropsNames.js 含 ".pragma library"，被 QML 以相对路径 import 使用，不依赖 qmldir 条目。
set_source_files_properties(qml/PropsNames.js PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)

# WIN32：将 exe 标记为 Windows GUI 子系统，避免启动时弹出空的 cmd/控制台窗口。
# 需要命令行诊断时仍可加 --console 参数，程序会自行 AllocConsole 打开控制台。
qt_add_executable(HMCSM-GUI-Local
    WIN32
    main.cpp
    appcontroller.h
    appcontroller.cpp
    app.rc
)

# Linux 上可执行文件不带 .exe 后缀，会与 qt_add_qml_module 在 build 根生成的同名
# QML 模块目录冲突，导致链接器 "cannot open output file ... Is a directory"。
if(NOT WIN32)
    set_target_properties(HMCSM-GUI-Local PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# QML 模块：UI 层单独抽离到 qml/，C++ 通过上下文属性注入，不在模块内注册类型。
# GLOB + CONFIGURE_DEPENDS：新增 .qml/.js 无需手动登记（但需重新 cmake configure）。
# 模块 URI 保持 "MinecraftServerManager"（现有所有 QML `import` 与 C++ `loadFromModule`
# 均据此引用），仅构建目标名改为 HMCSM-GUI-Local，不影响界面代码。
file(GLOB QML_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    qml/*.qml
    qml/*.js
)
qt_add_qml_module(HMCSM-GUI-Local
    URI MinecraftServerManager
    VERSION 1.0
    QML_FILES
        ${QML_SOURCES}
    RESOURCES
        Resources.qrc
)

# 内核通过 hmcsm_kernel 的 PUBLIC 链接把 Management/Execution/Control 全部带入，
# 这里只需显式声明 Qt 组件 + 内核 + 两个 shared 模块。
target_link_libraries(HMCSM-GUI-Local PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::WebSockets
    Qt6::Multimedia
    hmcsm_kernel
    hmcsm_logging
    hmcsm_appearance
    hmcsm_loader
)

target_include_directories(HMCSM-GUI-Local PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)

install(TARGETS HMCSM-GUI-Local
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
