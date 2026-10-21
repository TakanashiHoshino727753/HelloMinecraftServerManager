// ============================================================================
// HMCSM 前端侧后端加载器实现（见 hmcsm_loader.h）
// ============================================================================

#include "hmcsm_loader.h"

#include <QLibrary>
#include <QCoreApplication>

HmcsmLoader::HmcsmLoader(QObject *parent) : QObject(parent) {}

bool HmcsmLoader::load(const QString &backendLib)
{
    if (backendLib.isEmpty()) {
        // 解析当前进程：Qt 后端已静态链入 exe，符号就在本模块内。
        m_lib.setFileName(QCoreApplication::applicationFilePath());
    } else {
        m_lib.setFileName(backendLib);
    }

    if (!m_lib.load())
        return false;

    m_create = reinterpret_cast<FnCreate>(m_lib.resolve("hmcsm_kernel_create"));
    m_free   = reinterpret_cast<FnFree>(m_lib.resolve("hmcsm_kernel_free"));
    m_start  = reinterpret_cast<FnStart>(m_lib.resolve("hmcsm_kernel_start"));
    m_stop   = reinterpret_cast<FnStop>(m_lib.resolve("hmcsm_kernel_stop"));
    m_send   = reinterpret_cast<FnSend>(m_lib.resolve("hmcsm_kernel_send"));

    m_loaded = m_create && m_free && m_start && m_stop && m_send;
    return m_loaded;
}
