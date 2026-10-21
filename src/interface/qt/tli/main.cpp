// ============================================================================
// Interface / TLI（Text Line Interface）
// ----------------------------------------------------------------------------
// 交互式文本行会话：从 stdin 逐行读指令、把结果写回 stdout，直到输入 quit。
// 与 GUI / CLI 共用同一个内核与同一套 LocalConnection。
//
// 实现要点：stdin 的阻塞读取放在工作线程里，结果通过信号排队回主线程执行，
// 避免阻塞 Qt 事件循环（内核的下载 / 服务器状态回调都依赖事件循环）。
//
// 支持指令：help / services / servers / start / stop / send / console / resource / quit
// ============================================================================

#include <QCoreApplication>
#include <QNetworkProxyFactory>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVariant>

#include "hmcsm_logging.h"
#include "kernel.h"
#include "localconnection.h"

namespace {

// 工作线程：只负责阻塞读 stdin，并通过信号把整行交给主线程处理
class StdinReader : public QObject
{
    Q_OBJECT
public:
    explicit StdinReader(QObject *parent = nullptr) : QObject(parent) {}

    void run()
    {
        QTextStream in(stdin);
        forever {
            const QString line = in.readLine();
            if (line.isNull())
                break;
            emit lineReady(line.trimmed());
        }
        emit finished();
    }

signals:
    void lineReady(const QString &line);
    void finished();
};

QString serverPathFor(Kernel *kernel, const QString &name)
{
    for (const QVariant &v : kernel->serverManager()->serverSummary()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() == name)
            return m.value(QStringLiteral("path")).toString();
    }
    return QString();
}

void printHelp(QTextStream &out)
{
    out << "HMCSM 文本行接口（输入 quit 退出）\n"
        << "  services            列出内核服务\n"
        << "  servers             列出服务器与运行状态\n"
        << "  start <名称>        启动服务器\n"
        << "  stop <名称>         停止服务器\n"
        << "  send <名称> <指令>  发送控制台指令\n"
        << "  console <名称>      打印控制台输出\n"
        << "  resource            打印系统资源与配额\n";
    out.flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("HMCSM"));
    app.setApplicationName(QStringLiteral("MinecraftServerManager"));
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    hmcsmInstallLogging(QStringLiteral("hmcsm.log"), QStringLiteral("hmcsm-debug.log"));

    Kernel kernel;
    kernel.start();
    LocalConnection local(&kernel);

    QTextStream out(stdout);
    printHelp(out);
    out << "hmcsm> ";
    out.flush();

    QThread worker;
    StdinReader reader;
    reader.moveToThread(&worker);

    QObject::connect(&worker, &QThread::started, &reader, &StdinReader::run);
    QObject::connect(&reader, &StdinReader::lineReady, &app, [&](const QString &line) {
        if (line.isEmpty()) {
            out << "hmcsm> ";
            out.flush();
            return;
        }
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString cmd = parts.first();

        auto needName = [&](QString *name, QString *path) -> bool {
            if (parts.size() < 2) { out << "缺少服务器名称\n"; return false; }
            *name = parts.at(1);
            *path = serverPathFor(&kernel, *name);
            if (path->isEmpty()) { out << "未找到服务器：" << *name << "\n"; return false; }
            return true;
        };

        if (cmd == QStringLiteral("quit") || cmd == QStringLiteral("exit")) {
            kernel.shutdown();
            app.quit();
            return;
        } else if (cmd == QStringLiteral("help")) {
            printHelp(out);
        } else if (cmd == QStringLiteral("services")) {
            const QVariantMap snap = local.servicesSnapshot();
            for (auto it = snap.constBegin(); it != snap.constEnd(); ++it)
                out << it.key() << " -> " << it.value().toString() << "\n";
        } else if (cmd == QStringLiteral("servers")) {
            for (const QVariant &v : kernel.serverManager()->serverSummary()) {
                const QVariantMap m = v.toMap();
                const QString path = m.value(QStringLiteral("path")).toString();
                out << (kernel.serverController()->isRunning(path) ? "[运行中] " : "[已停止] ")
                    << m.value(QStringLiteral("name")).toString() << "\n";
            }
        } else if (cmd == QStringLiteral("resource")) {
            auto *ra = kernel.resourceAllocator();
            out << "CPU " << ra->cpuCoreCount() << " 核 / 内存 " << ra->totalMemoryMb()
                << " MB / 并行下载 " << ra->maxParallelDownloads()
                << " / " << kernel.processPrivilege()->summary() << "\n";
        } else if (cmd == QStringLiteral("start") || cmd == QStringLiteral("stop")
                   || cmd == QStringLiteral("send") || cmd == QStringLiteral("console")) {
            QString name, path;
            if (needName(&name, &path)) {
                if (cmd == QStringLiteral("start"))
                    kernel.serverController()->start(name, path);
                else if (cmd == QStringLiteral("stop"))
                    kernel.serverController()->stop(path);
                else if (cmd == QStringLiteral("send"))
                    kernel.serverController()->send(path, parts.mid(2).join(QLatin1Char(' ')));
                else
                    out << kernel.serverController()->getConsole(path);
            }
        } else {
            out << "未知指令：" << cmd << "（输入 help 查看）\n";
        }
        out << "hmcsm> ";
        out.flush();
    }, Qt::QueuedConnection);

    QObject::connect(&reader, &StdinReader::finished, &app, &QCoreApplication::quit);
    QObject::connect(&worker, &QThread::finished, &worker, &QObject::deleteLater);

    worker.start();
    const int rc = app.exec();
    worker.quit();
    worker.wait();
    return rc;
}

#include "main.moc"
