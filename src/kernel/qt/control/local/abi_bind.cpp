// ============================================================================
// Kernel / Qt / Control / Local —— C ABI 绑定
// ----------------------------------------------------------------------------
// 把 Qt 内核（Kernel + LocalConnection）包成 contract/hmcsm_abi.h 声明的 C 函数。
// 这样前端只需持有 HmcsmKernel* 不透明句柄，无需知道 Kernel 的具体类型。
//
// 同语言前端（Qt↔Qt）可在进程内直接链接本目标（Local 模式）；异语言前端则通过
// QLibrary 加载导出了同名 C 函数的后端动态库（Qt 后端即 hmcsm_kernel_qt，Rust 后端
// 即 hmcsm_kernel_rust）。两者 API 形状完全一致。
// ============================================================================

#include "hmcsm_abi.h"

#include <QMetaObject>
#include <QString>

#include "kernel.h"
#include "localconnection.h"

// 不透明句柄的具体定义（与 hmcsm_abi.h 的前向声明处于同一全局命名空间）。
struct HmcsmKernel {
    Kernel* kernel = nullptr;
    LocalConnection* conn = nullptr;
    QObject* serverCtl = nullptr;   // 解析出的 "serverController" 服务
};

// serverController.start 的默认参数（与 ServerController 声明保持一致）
constexpr int kDefaultMinMem = 1024;
constexpr int kDefaultMaxMem = 2048;

extern "C" {

HmcsmKernel* hmcsm_kernel_create(const char* /*cfg_path*/)
{
    auto* k = new HmcsmKernel;
    k->kernel = new Kernel();
    k->kernel->start();                       // 初始化并注册全部服务
    k->conn = new LocalConnection(k->kernel);
    k->serverCtl = k->conn->service(QStringLiteral("serverController"));
    return k;
}

void hmcsm_kernel_free(HmcsmKernel* k)
{
    if (!k)
        return;
    delete k->conn;
    delete k->kernel;
    delete k;
}

void hmcsm_kernel_start(HmcsmKernel* k, const char* server, const char* path)
{
    if (!k || !k->serverCtl)
        return;
    QMetaObject::invokeMethod(k->serverCtl, "start",
        Q_ARG(QString, QString::fromUtf8(server)),
        Q_ARG(QString, QString::fromUtf8(path)),
        Q_ARG(QString, QStringLiteral("java")),
        Q_ARG(int, kDefaultMinMem),
        Q_ARG(int, kDefaultMaxMem),
        Q_ARG(bool, true));
}

void hmcsm_kernel_stop(HmcsmKernel* k, const char* path)
{
    if (!k || !k->serverCtl)
        return;
    QMetaObject::invokeMethod(k->serverCtl, "stop",
        Q_ARG(QString, QString::fromUtf8(path)));
}

void hmcsm_kernel_send(HmcsmKernel* k, const char* path, const char* cmd)
{
    if (!k || !k->serverCtl)
        return;
    QMetaObject::invokeMethod(k->serverCtl, "send",
        Q_ARG(QString, QString::fromUtf8(path)),
        Q_ARG(QString, QString::fromUtf8(cmd)));
}

} // extern "C"
