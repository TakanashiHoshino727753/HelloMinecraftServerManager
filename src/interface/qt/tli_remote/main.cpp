// ============================================================================
// Interface / TLI-Remote（远程文本行接口）
// ----------------------------------------------------------------------------
// 交互式文本行会话：从 stdin 逐行读指令、把结果写回 stdout，直到输入 quit。
// 与 TLI-Local 等价的交互体验，但连的是「远端 HMCSM 实例」—— 经 hmcsm_remoteclient
// 访问本地端 WebUI JSON API。身份字段（服务器 name）由 API 原样返回并回传。
//
// 连接参数仅在启动时通过 --connect 提供，会话期间保持不变：
//   HMCSM-TLI-Remote --connect hmcsm://token@host:port
//   HMCSM-TLI-Remote --host h --port p --token t [--no-tls]
//   （无显式连接参数时复用已存储的远控配对凭证自动连接）
// 支持指令：help / servers / system / start / stop / forcestop / send / console / quit
// ============================================================================

#include <QCoreApplication>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QTextStream>
#include <QThread>
#include <QObject>

#include "remoteclient.h"
#include "hmcsm_logging.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

QJsonObject await(RemoteClient *c, QNetworkReply *r)
{
    QEventLoop loop;
    QTimer t; t.setSingleShot(true);
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(20000);
    loop.exec();
    QJsonObject res;
    if (r->error() == QNetworkReply::NoError) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll(), &pe);
        if (doc.isObject()) res = doc.object();
        else if (doc.isArray()) res = QJsonObject{{QStringLiteral("array"), doc.array()}};
    } else {
        res = QJsonObject{{QStringLiteral("error"), r->errorString()}};
    }
    r->deleteLater();
    return res;
}

QString enc(const QString &s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); }

// 在交互会话内执行一条指令；stdin 读取已在外层线程完成，此处直接发请求并打印。
void execRemote(RemoteClient *c, QTextStream &out, const QStringList &parts)
{
    if (parts.isEmpty()) return;
    const QString cmd = parts.first();
    const QStringList rest = parts.mid(1);

    if (cmd == QStringLiteral("quit") || cmd == QStringLiteral("exit")) {
        return;
    } else if (cmd == QStringLiteral("help")) {
        out << "HMCSM 远程文本行接口（输入 quit 退出）\n"
            << "  servers              列出服务器与运行状态\n"
            << "  system               打印远程系统资源占用\n"
            << "  start <名称>         启动服务器\n"
            << "  stop <名称>          优雅停止服务器\n"
            << "  forcestop <名称>     强制停止服务器\n"
            << "  send <名称> <指令>   发送控制台指令\n"
            << "  console <名称>       打印控制台输出\n";
    } else if (cmd == QStringLiteral("servers")) {
        QJsonObject o = await(c, c->apiGet(QStringLiteral("/api/servers")));
        if (o.contains(QStringLiteral("error"))) { out << o.value(QStringLiteral("error")).toString() << "\n"; return; }
        const QJsonArray arr = o.value(QStringLiteral("servers")).toArray();
        if (arr.isEmpty()) out << "(无服务器)\n";
        for (const QJsonValue &v : arr) {
            const QJsonObject s = v.toObject();
            out << (s.value(QStringLiteral("running")).toBool() ? "[运行中] " : "[已停止] ")
                << s.value(QStringLiteral("name")).toString() << "  "
                << s.value(QStringLiteral("version")).toString() << "  type="
                << s.value(QStringLiteral("type")).toString() << "\n";
        }
    } else if (cmd == QStringLiteral("system")) {
        QJsonObject o = await(c, c->apiGet(QStringLiteral("/api/system")));
        if (o.contains(QStringLiteral("error"))) { out << o.value(QStringLiteral("error")).toString() << "\n"; return; }
        const QJsonObject sys = o.value(QStringLiteral("system")).toObject();
        out << "CPU 使用率 : " << sys.value(QStringLiteral("cpu")).toDouble() << "%\n"
            << "内存使用   : " << sys.value(QStringLiteral("mem")).toDouble() << "%\n";
    } else if (cmd == QStringLiteral("start") || cmd == QStringLiteral("stop")
               || cmd == QStringLiteral("forcestop") || cmd == QStringLiteral("send")
               || cmd == QStringLiteral("console")) {
        if (rest.isEmpty()) { out << "缺少服务器名称\n"; return; }
        const QString base = QStringLiteral("/api/servers/") + enc(rest.first()) + QStringLiteral("/");
        QVariantMap query;
        QString path;
        if (cmd == QStringLiteral("start")) { path = base + QStringLiteral("start"); query[QStringLiteral("min")] = 1024; query[QStringLiteral("max")] = 2048; }
        else if (cmd == QStringLiteral("stop")) path = base + QStringLiteral("stop");
        else if (cmd == QStringLiteral("forcestop")) path = base + QStringLiteral("forcestop");
        else if (cmd == QStringLiteral("send")) { if (rest.size() < 2) { out << "缺少指令\n"; return; } path = base + QStringLiteral("send"); query[QStringLiteral("cmd")] = rest.mid(1).join(QLatin1Char(' ')); }
        else path = base + QStringLiteral("console");
        QJsonObject o = await(c, c->apiGet(path, query));
        if (o.contains(QStringLiteral("error"))) { out << o.value(QStringLiteral("error")).toString() << "\n"; return; }
        if (cmd == QStringLiteral("console")) {
            const QString con = o.value(QStringLiteral("console")).toString();
            out << con;
            if (!con.endsWith(QLatin1Char('\n'))) out << "\n";
        } else {
            out << "OK: " << cmd << " " << rest.first() << "\n";
        }
    } else {
        out << "未知指令：" << cmd << "（输入 help 查看）\n";
    }
}

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
            if (line.isNull()) break;
            emit lineReady(line.trimmed());
        }
        emit finished();
    }

signals:
    void lineReady(const QString &line);
    void finished();
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("HMCSM"));
    app.setApplicationName(QStringLiteral("MinecraftServerManager"));
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    hmcsmInstallLogging(QStringLiteral("hmcsm-remote.log"), QStringLiteral("hmcsm-remote-debug.log"));

    // —— 解析连接参数（仅连接相关，会话期间不变）——
    const QStringList args = app.arguments().mid(1);
    QString host, token;
    int port = 25575;
    bool https = true;
    bool explicitGiven = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QStringLiteral("--connect") || a == QStringLiteral("--uri")) {
            if (++i >= args.size()) { err << "缺少连接串\n"; return 2; }
            QVariantMap m;
            RemoteClient probe;
            if (!probe.parseUri(args.at(i), m)) { err << "无法解析连接串：" << args.at(i) << "\n"; return 2; }
            host = m.value(QStringLiteral("host")).toString();
            port = m.value(QStringLiteral("port")).toInt();
            token = m.value(QStringLiteral("token")).toString();
            https = m.value(QStringLiteral("https"), true).toBool();
            explicitGiven = true;
        } else if (a == QStringLiteral("--host")) {
            if (++i >= args.size()) { err << "缺少 host\n"; return 2; }
            host = args.at(i); explicitGiven = true;
        } else if (a == QStringLiteral("--port")) {
            if (++i >= args.size()) { err << "缺少 port\n"; return 2; }
            port = args.at(i).toInt(); explicitGiven = true;
        } else if (a == QStringLiteral("--token")) {
            if (++i >= args.size()) { err << "缺少 token\n"; return 2; }
            token = args.at(i); explicitGiven = true;
        } else if (a == QStringLiteral("--no-tls")) {
            https = false;
        }
    }

    RemoteClient client;
    const QString ch = explicitGiven ? host : client.host();
    const int cp = explicitGiven ? port : client.port();
    const QString ct = explicitGiven ? token : client.token();
    const bool chs = explicitGiven ? https : client.https();
    if (ch.isEmpty() || ct.isEmpty()) {
        err << "缺少连接参数（--connect 或已存储配对）\n";
        return 2;
    }

    // 连不上直接退出（交互式会话前必须建立连接）
    QEventLoop cloop;
    QMetaObject::Connection cconn = QObject::connect(&client, &RemoteClient::connectedChanged, [&]() {
        if (client.connected()) cloop.quit();
    });
    QTimer ctimer; ctimer.setSingleShot(true);
    QObject::connect(&ctimer, &QTimer::timeout, &cloop, &QEventLoop::quit);
    ctimer.start(20000);
    client.connectTo(ch, cp, ct, chs);
    cloop.exec();
    QObject::disconnect(cconn);
    if (!client.connected()) {
        err << "连接失败：" << client.lastError() << "\n";
        return 3;
    }

    out << "已连接 " << ch << ":" << cp << "（输入 help 查看指令，quit 退出）\n";
    out << "hmcsm-remote> ";
    out.flush();

    QThread worker;
    StdinReader reader;
    reader.moveToThread(&worker);

    QObject::connect(&worker, &QThread::started, &reader, &StdinReader::run);
    QObject::connect(&reader, &StdinReader::lineReady, &app, [&](const QString &line) {
        if (line.isEmpty()) { out << "hmcsm-remote> "; out.flush(); return; }
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.first() == QStringLiteral("quit") || parts.first() == QStringLiteral("exit")) {
            app.quit();
            return;
        }
        execRemote(&client, out, parts);
        out << "hmcsm-remote> ";
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
