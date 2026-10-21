# Download Manager（下载管理器）
#   httpclient.h        Link Acquisition 的网络原语（统一 HTTP GET + 硬超时闸门）
#   downloadcatalog.*   Link Acquisition（Java / 服务端 / Modrinth 目录解析）
#   downloadmanager.h   Download Tool（并行下载 / 断点续传 / 暂停 / 取消）
#   downloadlistmodel.h Download Tool 的任务列表模型
add_library(hmcsm_download STATIC
    httpclient.h
    downloadlistmodel.h
    downloadmanager.h
    downloadcatalog.h
    downloadcatalog.cpp
)
target_link_libraries(hmcsm_download PUBLIC Qt6::Core Qt6::Network hmcsm_java)
target_include_directories(hmcsm_download PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
