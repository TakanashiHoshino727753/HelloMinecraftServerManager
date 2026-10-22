# ============================================================================
# HMCSM 远控端子项目（Qt / C++ / QML 客户端）
# ----------------------------------------------------------------------------
# 与本地端（desktop）通过 WebUI JSON API（HTTPS + Bearer 令牌）通信，实现
# 远程管理 Minecraft 服务器：连接/列表/启停/发指令/一键优化模组安装。
# 与 desktop 共用同一份统一日志处理器（src/shared/hmcsm_logging.cpp），由 main.cpp 安装。
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(HMCSM-GUI-Remote VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT DEFINED QT_VERSION_MAJOR)
    set(QT_VERSION_MAJOR 6)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 Network Multimedia Gui Widgets Svg)

qt_standard_project_setup()

# 先构建远控端各业务领域的独立静态库（见 remoteclient/CMakeLists.txt）
add_subdirectory(remoteclient)

# hmcsm_appearance（appearancesettings）的 moc 由该静态库统一生成并编译；
# 远控端 main.cpp 仅引用该类，跳过其 AUTOMOC 扫描，避免重复生成 moc 导致链接多重定义。
set_source_files_properties(main.cpp PROPERTIES SKIP_AUTOMOC ON)

set(PROJECT_SOURCES
    main.cpp
    remotecontrollers/remoteserver.h
    remotecontrollers/remoteserver.cpp
    remotecontrollers/remotesystem.h
    remotecontrollers/remotesystem.cpp
    remotecontrollers/remoteapp.h
    remotecontrollers/remoteapp.cpp
    remotecontrollers/remoteproxycontroller.h
    remotecontrollers/remoteproxycontroller.cpp
    remotecontrollers/remoteproxymanager.h
    remotecontrollers/remoteproxymanager.cpp
    remotecontrollers/remotedownload.h
    remotecontrollers/remotedownload.cpp
    remotecontrollers/remoteinstall.h
    remotecontrollers/remoteinstall.cpp
    remotecontrollers/remotesettings.h
    remotecontrollers/remotesettings.cpp
    remotecontrollers/remotemaintenance.h
    remotecontrollers/remotemaintenance.cpp
)

# QML / 资源打进 qrc，跨平台统一从 qrc 加载
qt_add_executable(HMCSM-GUI-Remote
    ${PROJECT_SOURCES}
    remotecontrollers/remotewebui.cpp
    qml.qrc
    ${CMAKE_SOURCE_DIR}/src/interface/qt/gui/Resources.qrc
)

set_target_properties(HMCSM-GUI-Remote PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries(HMCSM-GUI-Remote PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Multimedia
    Qt6::Gui
    Qt6::Widgets
    hmcsm_remoteclient
    hmcsm_logging
    hmcsm_appearance
)

# 共享模块（src/shared/）头文件搜索路径
target_include_directories(HMCSM-GUI-Remote PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)

# ===== 单一代码库：直接复用桌面端 qml/ 目录作为本端 QML 模块（不再是复制副本）=====
# 两端共用同一套 QML（src/interface/qt/gui/qml/*.qml, *.js）；远控端仅额外补充 RemoteLinkDialog.qml。
# 有区别的部分（如配对/连接段）在共用 QML 内用运行时开关区分，无需维护两份文件。
set(HMCSM_SHARED_QML_DIR ${CMAKE_SOURCE_DIR}/src/interface/qt/gui/qml)
set_source_files_properties(${HMCSM_SHARED_QML_DIR}/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${HMCSM_SHARED_QML_DIR}/I18n.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${HMCSM_SHARED_QML_DIR}/PropsNames.js PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)
file(GLOB QML_SOURCES CONFIGURE_DEPENDS
    ${HMCSM_SHARED_QML_DIR}/*.qml
    ${HMCSM_SHARED_QML_DIR}/*.js
)
list(APPEND QML_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/qml/RemoteLinkDialog.qml)
# 跨目录复用 QML 源文件时必须显式指定资源别名（Qt 不允许绝对路径直接进资源），
# 别名用文件名即可：模块内统一为 qrc:/qt/qml/MinecraftServerManager/<文件名>。
foreach(hmcsm_qml ${QML_SOURCES})
    get_filename_component(hmcsm_qml_name ${hmcsm_qml} NAME)
    set_source_files_properties(${hmcsm_qml} PROPERTIES
        QT_RESOURCE_ALIAS ${hmcsm_qml_name}
        QT_QML_SKIP_CACHEGEN TRUE)
endforeach()
qt_add_qml_module(HMCSM-GUI-Remote
    URI MinecraftServerManager
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        ${QML_SOURCES}
)

# 安卓 / iOS 部署设置
if(ANDROID)
    set_property(TARGET HMCSM-GUI-Remote APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif()
