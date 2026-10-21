# 本地 Web 管理面板（默认端口 25575）：HTTP(S) + 自签证书 + 内嵌 SPA + /api/* JSON API
add_library(hmcsm_webui STATIC
    webuiserver.h
    webuiserver.cpp
)
# webuiserver_spa.inc 是被 webuiserver.cpp 包含的片段，不作为独立编译单元
set_source_files_properties(webuiserver_spa.inc PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(hmcsm_webui PRIVATE webuiserver_spa.inc)

# 二维码库（配对二维码用）被 webui 与 pairing 共用，位于同级 qrcode/ 模块

# 说明：hmcsm_ops / hmcsm_proxy 过去靠相对路径 #include 隐式依赖（CMake 未声明，
# 因为最终 exe 链了全部静态库所以链接期不报错，但模块边界已被破坏）。
# 分层后显式声明，依赖方向保持单向：control -> execution。
# crypt32 / advapi32 是 Windows CryptoAPI 专属库（自签证书），仅 Windows 链接。
if(WIN32)
    target_link_libraries(hmcsm_webui PUBLIC Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick crypt32 advapi32
        hmcsm_download hmcsm_server hmcsm_server hmcsm_java hmcsm_settings hmcsm_bot hmcsm_ops hmcsm_proxy hmcsm_qrcode)
else()
    target_link_libraries(hmcsm_webui PUBLIC Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick
        hmcsm_download hmcsm_server hmcsm_server hmcsm_java hmcsm_settings hmcsm_bot hmcsm_ops hmcsm_proxy hmcsm_qrcode)
endif()
target_include_directories(hmcsm_webui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
