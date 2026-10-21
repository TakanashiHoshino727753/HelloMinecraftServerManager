cmake_minimum_required(VERSION 3.16)

# ============================================================================
# HMCSM 父项目（聚合 / Superbuild）
# ----------------------------------------------------------------------------
# 本仓库按「接口层(Interface) — 内核(Kernel)」两层组织，内核再分三层：
#
#   src/interface/   接口层：按语言分组的多前端（均经契约访问内核）
#       qt/          Qt(C++/QML) 前端：gui / cli / tli / remote / cli_remote / tli_remote
#           gui/         Desktop GUI —— 构建目标 HMCSM-GUI-Local
#           cli/         Command Line Interface —— HMCSM-CLI-Local
#           tli/         Text Line Interface —— HMCSM-TLI-Local
#           remote/      远控端（Qt/QML，经 WebUI API 远程管理）—— HMCSM-GUI-Remote
#           cli_remote/ / tli_remote/  远程命令行 / 文本行接口 —— HMCSM-CLI-Remote / HMCSM-TLI-Remote
#       lua/         Lua 前端（脚本，经 WebUI REST API 对接任意后端，不纳入 CMake 构建）
#
#   src/kernel/      内核：与前端无关的全部能力，按语言分组双实现
#       contract/   契约层（语言无关 C ABI：hmcsm_abi.h）—— 前后端唯一耦合点
#       qt/         Qt(C++) 内核实现
#           control/     控制层：local/(进程内直连，组合根 Kernel) / remote/(webui/pairing/napcat)
#           management/  管理层：modulebus / diskaccess / processprivilege / resourceallocation
#           execution/   执行层：download / java / server / settings / monitor / ops / proxy / notify / database / ai / interfaces
#       rust/       Rust 内核实现（Cargo workspace，独立编译为 libhmcsm_kernel，见 rust/README）
#
#   src/shared/      两端共用：logging（统一日志）、appearance（外观个性化）
#
# 依赖方向严格单向：Interface -> Kernel.Control -> Kernel.Execution -> Kernel.Management。
# 已建成形，反向依赖（execution -> control）属于架构违规，不可引入。
#
# 构建： cmake -S . -B build && cmake --build build
# ============================================================================
project(HelloMinecraftServerManager
    VERSION 1.0
    DESCRIPTION "HMCSM 父项目：接口层(interface) + 内核(kernel) 两层结构"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 元对象系统（moc/rcc/uic）全局开启，内核子目录的静态库同样受益
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 未显式指定构建类型时默认 Release。
# 注意：本机 Qt 只有 Release 运行时，Debug 构建会在 QApplication 构造附近 SIGSEGV。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "默认构建类型" FORCE)
endif()

# 跳过 Qt6Gui 对 WrapVulkanHeaders 的探测：本项目只用 OpenGL 渲染后端
#（见 main.cpp 的 QSG_RHI_BACKEND=opengl），无需 Vulkan 头文件/SDK。
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)

# Qt 依赖在此统一探测：内核（静态库）与接口层（可执行文件）共用同一批组件。
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Quick
    QuickControls2
    Widgets
    Network
    Svg
    WebSockets
    HttpServer
    Multimedia
)

qt_standard_project_setup(REQUIRES 6.8)

# 共用静态库：统一日志 + 外观个性化（须在内核之前构建）
add_subdirectory(src/shared)

# 内核：management -> execution -> control
add_subdirectory(src/kernel)

# 接口层：gui / cli / tli / remote
add_subdirectory(src/interface)
