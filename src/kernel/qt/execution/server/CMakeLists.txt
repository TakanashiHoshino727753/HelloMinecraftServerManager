# 服务器运行（ServerManager 数据模型 + ServerController 门面与其子模块）
#
# 按架构「Server Manager」节点拆分：
#   servermanager.h           ServerManager 数据模型（QQmlListProperty 暴露给 QML）
#   servercontroller.*        门面：聚合并转发下列子模块，对接口层透明
#   servercontrol.*          Server Control（进程运行时）
#   serverconfig.*           Server Configuration File Management
#   processconstruction.*    Process Construction（启动参数自适应探测）
#   serverinfo.*             Server Information Processing（CPU/内存采样）
#   commandtools.*           Command Tools（Instruction Set / Command Completion）
#   playerinfo.*             Player（信息获取 / 组织）
#   worldinfo.*              World（信息获取 / 组织 / 备份恢复）
#   modsmanager.*            Mods（支持检测 / 列表 / 冲突排除）
#   watchdog.*               WatchDog（卡死双检测）
#   autocorrection.*         Auto Correction（异常纠错中心）
#   createservercontroller.* Server Creation（依赖匹配 / 依赖补全）
#   installcoordinator.* / installtask.h  安装协调（配合 Server Creation）
#   modpackimporter.*        Package Import（内核识别 / 依赖匹配 / 依赖补全）
#
# 文件按字母序列出，便于核对；逻辑依赖由代码（而非列出顺序）决定。
add_library(hmcsm_server STATIC
    autocorrection.cpp
    autocorrection.h
    commandtools.cpp
    commandtools.h
    modsmanager.cpp
    modsmanager.h
    playerinfo.cpp
    playerinfo.h
    processconstruction.cpp
    processconstruction.h
    serverconfig.cpp
    serverconfig.h
    servercontrol.cpp
    servercontrol.h
    servercontroller.cpp
    servercontroller.h
    serverinfo.cpp
    serverinfo.h
    servermanager.h
    watchdog.cpp
    watchdog.h
    worldinfo.cpp
    worldinfo.h
    # —— Server Creation（依赖匹配 / 依赖补全）——
    createservercontroller.cpp
    createservercontroller.h
    installcoordinator.cpp
    installcoordinator.h
    installtask.h
    # —— Package Import（内核识别 / 依赖匹配 / 依赖补全）——
    modpackimporter.cpp
    modpackimporter.h
)
# Psapi 是 Windows 专属库（进程/模块信息查询，ServerInfo 采样用），仅 Windows 链接。
if(WIN32)
    target_link_libraries(hmcsm_server PUBLIC Qt6::Core Qt6::Qml Qt6::Network Qt6::Gui Psapi hmcsm_download hmcsm_java)
else()
    target_link_libraries(hmcsm_server PUBLIC Qt6::Core Qt6::Qml Qt6::Network Qt6::Gui hmcsm_download hmcsm_java)
endif()
target_include_directories(hmcsm_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
