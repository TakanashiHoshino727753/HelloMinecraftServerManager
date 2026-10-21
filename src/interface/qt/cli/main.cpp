// ============================================================================
// Interface / CLI（Command Line Interface）
// ----------------------------------------------------------------------------
// 与 Desktop GUI 并列的第二个前端：同一个内核、同一套 LocalConnection，
// 只是把结果打印到 stdout 而不是画窗口。用来验证「换前端不需要重新接线」。
//
// 用法：
//   HMCSM_Cli services                 列出内核已注册的服务（名字 -> 类名）
//   HMCSM_Cli servers                  列出受管服务器及运行状态
//   HMCSM_Cli start <名称>             启动指定服务器（内存取资源分配策略的默认值）
//   HMCSM_Cli stop  <名称>             优雅停止指定服务器
//   HMCSM_Cli send  <名称> <指令>      向运行中的服务器发一条控制台指令
//   HMCSM_Cli console <名称>           打印该服务器最近的控制台输出
// ============================================================================

#include <QCoreApplication>
#include <QNetworkProxyFactory>
#include <QStringList>
#include <QTextStream>
#include <QVariant>

#include "hmcsm_logging.h"
#include "kernel.h"
#include "localconnection.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

void printUsage()
{
    out << "HMCSM 命令行接口\n"
        << "  services              列出内核服务\n"
        << "  servers               列出服务器与运行状态\n"
        << "  start <名称>          启动服务器\n"
        << "  stop <名称>           停止服务器\n"
        << "  send <名称> <指令>    发送控制台指令\n"
        << "  console <名称>        打印控制台输出\n"
        << "  resource              打印系统资源与配额\n";
    out.flush();
}

QString serverPathFor(Kernel *kernel, const QString &name)
{
    for (const QVariant &v : kernel->serverManager()->serverSummary()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() == name)
            return m.value(QStringLiteral("path")).toString();
    }
    return QString();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("HMCSM"));
    app.setApplicationName(QStringLiteral("MinecraftServerManager"));
    // 与 GUI 一致：读取系统代理，否则需要代理的环境下所有出站请求都会被重置
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    hmcsmInstallLogging(QStringLiteral("hmcsm.log"), QStringLiteral("hmcsm-debug.log"));

    // 内核装配：与 GUI 完全相同的一行，前端差异到此为止
    Kernel kernel;
    kernel.start();
    LocalConnection local(&kernel);

    const QStringList args = app.arguments().mid(1);
    const QString cmd = args.isEmpty() ? QStringLiteral("help") : args.first();

    if (cmd == QStringLiteral("services")) {
        const QVariantMap snap = local.servicesSnapshot();
        for (auto it = snap.constBegin(); it != snap.constEnd(); ++it)
            out << it.key() << " -> " << it.value().toString() << "\n";
        out.flush();
        return 0;
    }

    if (cmd == QStringLiteral("servers")) {
        for (const QVariant &v : kernel.serverManager()->serverSummary()) {
            const QVariantMap m = v.toMap();
            const QString name = m.value(QStringLiteral("name")).toString();
            const QString path = m.value(QStringLiteral("path")).toString();
            out << (kernel.serverController()->isRunning(path) ? "[运行中] " : "[已停止] ")
                << name << "  " << m.value(QStringLiteral("version")).toString()
                << "  " << path << "\n";
        }
        out.flush();
        return 0;
    }

    if (cmd == QStringLiteral("resource")) {
        auto *ra = kernel.resourceAllocator();
        out << "CPU 核心数      : " << ra->cpuCoreCount() << "\n"
            << "物理内存 (MB)   : " << ra->totalMemoryMb() << "\n"
            << "并行下载上限    : " << ra->maxParallelDownloads() << "\n"
            << "单服内存上限(MB): " << ra->maxMemoryPerServerMb() << "\n"
            << "进程权限        : " << kernel.processPrivilege()->summary() << "\n";
        out.flush();
        return 0;
    }

    if (cmd == QStringLiteral("start") || cmd == QStringLiteral("stop")
        || cmd == QStringLiteral("send") || cmd == QStringLiteral("console")) {
        if (args.size() < 2) {
            err << "缺少服务器名称\n";
            return 2;
        }
        const QString name = args.at(1);
        const QString path = serverPathFor(&kernel, name);
        if (path.isEmpty()) {
            err << "未找到服务器：" << name << "\n";
            return 2;
        }
        if (cmd == QStringLiteral("start")) {
            kernel.serverController()->start(name, path);
            out << "已请求启动：" << name << "\n";
        } else if (cmd == QStringLiteral("stop")) {
            kernel.serverController()->stop(path);
            out << "已请求停止：" << name << "\n";
        } else if (cmd == QStringLiteral("send")) {
            if (args.size() < 3) { err << "缺少指令\n"; return 2; }
            kernel.serverController()->send(path, args.mid(2).join(QLatin1Char(' ')));
            out << "已发送指令到：" << name << "\n";
        } else {
            out << kernel.serverController()->getConsole(path);
        }
        out.flush();
        return 0;
    }

    printUsage();
    return args.isEmpty() ? 1 : 0;
}
