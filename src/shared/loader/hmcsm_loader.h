#pragma once

// ============================================================================
// HMCSM 前端侧后端加载器（基于 Qt 的 QLibrary）
// ----------------------------------------------------------------------------
// 前端通过本类在「运行期」选择要使用的后端，而不是在编译期写死：
//   - backendLib 为空：解析当前进程内已静态链入的 Qt 后端（Qt↔Qt Local 模式）；
//   - backendLib = "hmcsm_kernel_rust"：QLibrary 加载 Rust 后端导出的同名 C ABI 动态库。
// 解析出的函数指针全部来自 contract/hmcsm_abi.h，因此前后端通过同一份契约解耦。
// ============================================================================

#include <QObject>
#include <QLibrary>
#include <QString>

#include "hmcsm_abi.h"

class HmcsmLoader : public QObject
{
    Q_OBJECT
public:
    explicit HmcsmLoader(QObject *parent = nullptr);

    // 加载后端。backendLib 形如 "hmcsm_kernel_rust"（不含扩展名）；为空则解析当前进程。
    bool load(const QString &backendLib = QString());

    bool isLoaded() const { return m_loaded; }

    HmcsmKernel* create(const QString &cfgPath) const
    {
        return m_create ? m_create(cfgPath.toUtf8().constData()) : nullptr;
    }
    void freeKernel(HmcsmKernel* k) const { if (m_free) m_free(k); }
    void start(HmcsmKernel* k, const QString &server, const QString &path) const
    {
        if (m_start) m_start(k, server.toUtf8().constData(), path.toUtf8().constData());
    }
    void stop(HmcsmKernel* k, const QString &path) const
    {
        if (m_stop) m_stop(k, path.toUtf8().constData());
    }
    void send(HmcsmKernel* k, const QString &path, const QString &cmd) const
    {
        if (m_send) m_send(k, path.toUtf8().constData(), cmd.toUtf8().constData());
    }

private:
    using FnCreate = HmcsmKernel*(*)(const char*);
    using FnFree   = void(*)(HmcsmKernel*);
    using FnStart  = void(*)(HmcsmKernel*, const char*, const char*);
    using FnStop   = void(*)(HmcsmKernel*, const char*);
    using FnSend   = void(*)(HmcsmKernel*, const char*, const char*);

    QLibrary m_lib;
    FnCreate m_create = nullptr;
    FnFree   m_free   = nullptr;
    FnStart  m_start  = nullptr;
    FnStop   m_stop   = nullptr;
    FnSend   m_send   = nullptr;
    bool m_loaded = false;
};
