// remoteproxycontroller.cpp —— 见 remoteproxycontroller.h
#include "remoteproxycontroller.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>

RemoteProxyController::RemoteProxyController(RemoteClient *rc, const QString &instanceId,
                                             const QString &name, QObject *parent)
    : QObject(parent), m_rc(rc), m_instanceId(instanceId), m_name(name)
{
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) refresh();
    });
}

QString RemoteProxyController::proxyDir() const
{
    // 远控端无本地代理目录概念，返回与桌面端一致的标识名（仅用于展示）
    return QStringLiteral("Velocity");
}

void RemoteProxyController::setProxyPort(int p)
{
    if (m_proxyPort == p) return;
    m_proxyPort = p;
    emit proxyPortChanged();
    postSet(QJsonObject{{QStringLiteral("proxyPort"), p}});
}

void RemoteProxyController::setMotd(const QString &m)
{
    if (m_motd == m) return;
    m_motd = m;
    emit motdChanged();
    postSet(QJsonObject{{QStringLiteral("motd"), m}});
}

void RemoteProxyController::setOfflineMode(bool v)
{
    if (m_offlineMode == v) return;
    m_offlineMode = v;
    emit offlineModeChanged();
    postSet(QJsonObject{{QStringLiteral("offlineMode"), v}});
}

void RemoteProxyController::setStopBackendsWithProxy(bool v)
{
    if (m_stopBackendsWithProxy == v) return;
    m_stopBackendsWithProxy = v;
    emit stopBackendsWithProxyChanged();
    postSet(QJsonObject{{QStringLiteral("stopBackendsWithProxy"), v}});
}

void RemoteProxyController::setAutoRestart(bool v)
{
    if (m_autoRestart == v) return;
    m_autoRestart = v;
    emit autoRestartChanged();
    postSet(QJsonObject{{QStringLiteral("autoRestart"), v}});
}

void RemoteProxyController::setName(const QString &n)
{
    if (m_name == n) return;
    m_name = n;
    emit nameChanged();
    postSet(QJsonObject{{QStringLiteral("name"), n}});
}

void RemoteProxyController::setServerFilter(const QStringList &f)
{
    if (m_serverFilter == f) return;
    m_serverFilter = f;
    emit serverFilterChanged();
    QJsonArray arr; for (const QString &s : f) arr.append(s);
    postSet(QJsonObject{{QStringLiteral("serverFilter"), arr}});
}

void RemoteProxyController::setServerEnabled(const QString &serverName, bool on)
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/setServer"),
                  QJsonObject{{QStringLiteral("name"), serverName}, {QStringLiteral("on"), on}});
    refresh();
}

void RemoteProxyController::startBackend(const QString &serverName)
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/startBackend"),
                  QJsonObject{{QStringLiteral("name"), serverName}});
    refresh();
}

void RemoteProxyController::stopBackend(const QString &serverName)
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/stopBackend"),
                  QJsonObject{{QStringLiteral("name"), serverName}});
    refresh();
}

void RemoteProxyController::install()
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/install"), QJsonObject());
    refresh();
}

bool RemoteProxyController::syncConfig()
{
    QNetworkReply *r = m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/sync"), QJsonObject());
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        refresh();
    });
    return true;   // 异步执行，远控端立即返回（桌面端同步结果已无意义）
}

void RemoteProxyController::start(const QString &javaPath)
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/start"),
                  QJsonObject{{QStringLiteral("javaPath"), javaPath}});
    refresh();
}

void RemoteProxyController::stop()
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/stop"), QJsonObject());
    refresh();
}

void RemoteProxyController::postSet(const QJsonObject &patch)
{
    m_rc->apiPost("/api/proxies/" + m_instanceId + QStringLiteral("/set"), patch);
}

void RemoteProxyController::refresh()
{
    QNetworkReply *r = m_rc->apiGet("/api/proxies/" + m_instanceId);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError)
            applyDetail(QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    });
}

void RemoteProxyController::applyDetail(const QJsonObject &o)
{
    const bool running = o.value(QStringLiteral("running")).toBool();
    if (running != m_running) { m_running = running; emit runningChanged(); }
    const bool installed = o.value(QStringLiteral("installed")).toBool();
    if (installed != m_installed) { m_installed = installed; emit installedChanged(); }
    const bool busy = o.value(QStringLiteral("busy")).toBool();
    if (busy != m_busy) { m_busy = busy; emit busyChanged(); }
    const QString status = o.value(QStringLiteral("status")).toString();
    if (status != m_status) { m_status = status; emit statusChanged(); }
    const int port = o.value(QStringLiteral("proxyPort")).toInt();
    if (port != m_proxyPort) { m_proxyPort = port; emit proxyPortChanged(); }
    const QString motd = o.value(QStringLiteral("motd")).toString();
    if (motd != m_motd) { m_motd = motd; emit motdChanged(); }
    const qreal prog = o.value(QStringLiteral("installProgress")).toDouble();
    if (qAbs(prog - m_installProgress) > 1e-4) { m_installProgress = prog; emit installProgressChanged(); }
    const bool off = o.value(QStringLiteral("offlineMode")).toBool();
    if (off != m_offlineMode) { m_offlineMode = off; emit offlineModeChanged(); }
    const bool sbp = o.value(QStringLiteral("stopBackendsWithProxy")).toBool();
    if (sbp != m_stopBackendsWithProxy) { m_stopBackendsWithProxy = sbp; emit stopBackendsWithProxyChanged(); }
    const bool ar = o.value(QStringLiteral("autoRestart")).toBool();
    if (ar != m_autoRestart) { m_autoRestart = ar; emit autoRestartChanged(); }
    const int pc = o.value(QStringLiteral("playerCount")).toInt();
    if (pc != m_playerCount) { m_playerCount = pc; emit playerCountChanged(); }
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type != m_type) m_type = type;
    const QString name = o.value(QStringLiteral("name")).toString();
    if (name != m_name) { m_name = name; emit nameChanged(); }
    const QStringList fl = o.value(QStringLiteral("serverFilter")).toVariant().toStringList();
    if (fl != m_serverFilter) { m_serverFilter = fl; emit serverFilterChanged(); }

    const QString console = o.value(QStringLiteral("console")).toString();
    if (console != m_console) {
        m_console = console;
        emit consoleAppended(console);
    }
    m_backendSummary = o.value(QStringLiteral("backends")).toArray().toVariantList();
    QVariantList bound;
    for (const QJsonValue &v : o.value(QStringLiteral("servers")).toArray())
        bound.append(v.toString());
    if (bound != m_serversBound) m_serversBound = bound;
}
