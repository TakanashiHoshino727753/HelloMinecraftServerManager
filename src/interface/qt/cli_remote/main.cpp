// ============================================================================
// Interface / CLI-Remote（远程命令行接口）
// ----------------------------------------------------------------------------
// 与 CLI-Local 等价的能力，但连的是「远端 HMCSM 实例」而非本地内核：
// 经远控端 REST 客户端（hmcsm_remoteclient）访问本地端的 WebUI JSON API（默认 25575）。
// 身份字段（服务器 name）由 API 原样返回并回传，与 GUI-Remote 行为一致。
//
// 用法：
//   HMCSM-CLI-Remote --connect hmcsm://token@host:port  servers
//   HMCSM-CLI-Remote --host h --port p --token t [--no-tls]  start <名称>
//   HMCSM-CLI-Remote --connect ...  system
//   HMCSM-CLI-Remote --connect ...  stop <名称>
//   HMCSM-CLI-Remote --connect ...  send <名称> <指令>
//   HMCSM-CLI-Remote --connect ...  console <名称>
//   （无显式连接参数时复用已存储的远控配对凭证自动连接）
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
#include <QObject>

#include "remoteclient.h"
#include "hmcsm_logging.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

// 同步等待一个 REST 回复（带 20s 超时），返回解析后的 JSON 对象。
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

bool connectAndWait(RemoteClient *c, const QString &host, int port, const QString &token, bool https)
{
    QEventLoop loop;
    QMetaObject::Connection conn = QObject::connect(c, &RemoteClient::connectedChanged, [&]() {
        if (c->connected()) loop.quit();
    });
    QTimer t; t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(20000);
    c->connectTo(host, port, token, https);
    loop.exec();
    QObject::disconnect(conn);
    return c->connected();
}

QString enc(const QString &s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); }

bool runServers(RemoteClient *c)
{
    QJsonObject o = await(c, c->apiGet(QStringLiteral("/api/servers")));
    if (o.contains(QStringLiteral("error"))) { err << o.value(QStringLiteral("error")).toString() << "\n"; return false; }
    const QJsonArray arr = o.value(QStringLiteral("servers")).toArray();
    if (arr.isEmpty()) out << "(无服务器)\n";
    for (const QJsonValue &v : arr) {
        const QJsonObject s = v.toObject();
        out << (s.value(QStringLiteral("running")).toBool() ? "[运行中] " : "[已停止] ")
            << s.value(QStringLiteral("name")).toString() << "  "
            << s.value(QStringLiteral("version")).toString() << "  type="
            << s.value(QStringLiteral("type")).toString() << "  path="
            << s.value(QStringLiteral("path")).toString() << "\n";
    }
    return true;
}

bool runSystem(RemoteClient *c)
{
    QJsonObject o = await(c, c->apiGet(QStringLiteral("/api/system")));
    if (o.contains(QStringLiteral("error"))) { err << o.value(QStringLiteral("error")).toString() << "\n"; return false; }
    const QJsonObject sys = o.value(QStringLiteral("system")).toObject();
    out << "CPU 使用率 : " << sys.value(QStringLiteral("cpu")).toDouble() << "%\n"
        << "内存使用   : " << sys.value(QStringLiteral("mem")).toDouble() << "%\n";
    return true;
}

bool runServerAction(RemoteClient *c, const QString &action, const QString &name, const QStringList &rest)
{
    const QString base = QStringLiteral("/api/servers/") + enc(name) + QStringLiteral("/");
    QVariantMap query;
    QString path;
    if (action == QStringLiteral("start")) {
        path = base + QStringLiteral("start");
        query[QStringLiteral("min")] = 1024;
        query[QStringLiteral("max")] = 2048;
    } else if (action == QStringLiteral("stop")) {
        path = base + QStringLiteral("stop");
    } else if (action == QStringLiteral("forcestop")) {
        path = base + QStringLiteral("forcestop");
    } else if (action == QStringLiteral("send")) {
        if (rest.isEmpty()) { err << "缺少指令\n"; return false; }
        path = base + QStringLiteral("send");
        query[QStringLiteral("cmd")] = rest.join(QLatin1Char(' '));
    } else if (action == QStringLiteral("console")) {
        path = base + QStringLiteral("console");
    } else {
        err << "未知动作：" << action << "\n";
        return false;
    }
    QJsonObject o = await(c, c->apiGet(path, query));
    if (o.contains(QStringLiteral("error"))) { err << o.value(QStringLiteral("error")).toString() << "\n"; return false; }
    if (action == QStringLiteral("console")) {
        const QString con = o.value(QStringLiteral("console")).toString();
        out << con;
        if (!con.endsWith(QLatin1Char('\n'))) out << "\n";
    } else {
        out << "OK: " << action << " " << name << "\n";
    }
    return true;
}

void printUsage()
{
    out << "HMCSM 远程命令行接口（经本地端 WebUI REST API 管理远程 HMCSM 实例）\n"
        << "连接参数（任选其一）：\n"
        << "  --connect <hmcsm://token@host:port>           连接串\n"
        << "  --host <h> --port <p> --token <t> [--no-tls]  显式参数（端口默认 25575，默认 HTTPS）\n"
        << "指令：\n"
        << "  servers              列出服务器与运行状态\n"
        << "  system               打印远程系统资源占用\n"
        << "  start <名称>         启动服务器\n"
        << "  stop <名称>          优雅停止服务器\n"
        << "  forcestop <名称>     强制停止服务器\n"
        << "  send <名称> <指令>   发送控制台指令\n"
        << "  console <名称>       打印控制台输出\n"
        << "（无显式连接参数时复用已存储的远控配对凭证自动连接）\n";
    out.flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("HMCSM"));
    app.setApplicationName(QStringLiteral("MinecraftServerManager"));
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    hmcsmInstallLogging(QStringLiteral("hmcsm-remote.log"), QStringLiteral("hmcsm-remote-debug.log"));

    const QStringList args = app.arguments().mid(1);
    QString host, token;
    int port = 25575;
    bool https = true;
    bool explicitGiven = false;
    QStringList positional;
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
        } else {
            positional.append(a);
        }
    }

    if (positional.isEmpty()) { printUsage(); return 1; }

    RemoteClient client;
    // 无显式连接参数时，复用构造时已 loadStored 的远控配对凭证
    const QString ch = explicitGiven ? host : client.host();
    const QString ct = explicitGiven ? token : client.token();
    const int cp = explicitGiven ? port : client.port();
    const bool chs = explicitGiven ? https : client.https();

    if (ch.isEmpty() || ct.isEmpty()) {
        err << "缺少连接参数（--connect 或已存储配对）\n";
        return 2;
    }
    if (!client.connected() && !connectAndWait(&client, ch, cp, ct, chs)) {
        err << "连接失败：" << client.lastError() << "\n";
        return 3;
    }

    const QString cmd = positional.first();
    const QStringList cmdArgs = positional.mid(1);

    bool ok = true;
    if (cmd == QStringLiteral("servers")) ok = runServers(&client);
    else if (cmd == QStringLiteral("system")) ok = runSystem(&client);
    else if (cmd == QStringLiteral("start") || cmd == QStringLiteral("stop") || cmd == QStringLiteral("forcestop")
             || cmd == QStringLiteral("send") || cmd == QStringLiteral("console")) {
        if (cmdArgs.isEmpty()) { err << "缺少服务器名称\n"; return 2; }
        ok = runServerAction(&client, cmd, cmdArgs.first(), cmdArgs.mid(1));
    } else {
        printUsage();
        return 1;
    }
    return ok ? 0 : 4;
}
