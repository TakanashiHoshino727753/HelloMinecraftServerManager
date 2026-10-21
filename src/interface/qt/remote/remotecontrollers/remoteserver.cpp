// remoteserver.cpp —— 见 remoteserver.h
#include "remoteserver.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>

static QString enc(const QString &s) { return QUrl::toPercentEncoding(s); }

// ---------------- RemoteServerManager ----------------
RemoteServerManager::RemoteServerManager(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    // 复用 RemoteClient 已有的 /api/servers 轮询结果，避免重复请求
    connect(m_rc, &RemoteClient::serversChanged, this, [this]() {
        // serversChanged 不带参；这里直接重新拉取一次列表以拿到完整字段
        QNetworkReply *r = m_rc->apiGet("/api/servers");
        connect(r, &QNetworkReply::finished, this, [this, r]() {
            if (r->error() == QNetworkReply::NoError) {
                QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
                rebuild(o.value("servers").toArray());
            }
            r->deleteLater();
        });
    });
}

QQmlListProperty<RemoteServer> RemoteServerManager::servers()
{
    return QQmlListProperty<RemoteServer>(this, nullptr,
                                          &RemoteServerManager::appendServer,
                                          &RemoteServerManager::serverCount,
                                          &RemoteServerManager::serverAt,
                                          &RemoteServerManager::clearServers);
}

void RemoteServerManager::rebuild(const QJsonArray &arr)
{
    qDeleteAll(m_servers);
    m_servers.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        auto *s = new RemoteServer(o.value("name").toString(),
                                   o.value("version").toString(),
                                   o.value("type").toString(),
                                   o.value("path").toString(),
                                   o.value("id").toString(),
                                   o.value("proxyId").toString(), this);
        m_servers.append(s);
    }
    emit serversChanged();
}

QVariantList RemoteServerManager::serverSummary() const
{
    QVariantList out;
    for (RemoteServer *s : m_servers) {
        QVariantMap m;
        m[QStringLiteral("name")] = s->name();
        m[QStringLiteral("version")] = s->version();
        m[QStringLiteral("type")] = s->type();
        m[QStringLiteral("path")] = s->path();
        m[QStringLiteral("id")] = s->id();
        m[QStringLiteral("proxyId")] = s->proxyId();
        out << m;
    }
    return out;
}

int RemoteServerManager::indexOfServer(const QString &name) const
{
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers.at(i)->name() == name) return i;
    return -1;
}

void RemoteServerManager::scanServers()
{
    QNetworkReply *r = m_rc->apiGet("/api/servers");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            rebuild(o.value("servers").toArray());
        }
        r->deleteLater();
    });
}

void RemoteServerManager::removeServer(int index)
{
    if (index < 0 || index >= m_servers.size()) return;
    const QString name = m_servers.at(index)->name();
    QNetworkReply *del = m_rc->apiDelete("/api/servers/" + enc(name));
    connect(del, &QNetworkReply::finished, this, [this, del]() {
        del->deleteLater();
        scanServers();
    });
}

bool RemoteServerManager::setServerProxy(const QString &name, const QString &proxyId)
{
    QNetworkReply *r = m_rc->apiPost("/api/servers/" + enc(name) + "/proxy",
                                     QJsonObject{{QStringLiteral("proxyId"), proxyId}});
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
    return true;
}

void RemoteServerManager::appendServer(QQmlListProperty<RemoteServer> *prop, RemoteServer *s)
{
    auto *self = qobject_cast<RemoteServerManager *>(prop->object);
    if (self) { self->m_servers.append(s); emit self->serversChanged(); }
}
RemoteServer *RemoteServerManager::serverAt(QQmlListProperty<RemoteServer> *prop, long long i)
{
    auto *self = qobject_cast<RemoteServerManager *>(prop->object);
    return self ? self->m_servers.at(static_cast<int>(i)) : nullptr;
}
long long RemoteServerManager::serverCount(QQmlListProperty<RemoteServer> *prop)
{
    auto *self = qobject_cast<RemoteServerManager *>(prop->object);
    return self ? self->m_servers.size() : 0;
}
void RemoteServerManager::clearServers(QQmlListProperty<RemoteServer> *prop)
{
    auto *self = qobject_cast<RemoteServerManager *>(prop->object);
    if (self) { qDeleteAll(self->m_servers); self->m_servers.clear(); emit self->serversChanged(); }
}

// ---------------- RemoteServerController ----------------
RemoteServerController::RemoteServerController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_listTimer = new QTimer(this);
    m_listTimer->setInterval(2000);
    connect(m_listTimer, &QTimer::timeout, this, &RemoteServerController::refreshList);
    m_sysTimer = new QTimer(this);
    m_sysTimer->setInterval(3000);
    connect(m_sysTimer, &QTimer::timeout, this, [this]() {
        QNetworkReply *r = m_rc->apiGet("/api/system");
        connect(r, &QNetworkReply::finished, this, [this, r]() {
            if (r->error() == QNetworkReply::NoError) {
                QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
                m_sysCpu = o.value("cpu").toDouble();
                m_sysMem = o.value("mem").toDouble();
            }
            r->deleteLater();
        });
    });
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { m_listTimer->start(); m_sysTimer->start(); refreshList(); }
        else  { m_listTimer->stop(); m_sysTimer->stop(); m_running.clear(); m_detail.clear(); }
    });
}

void RemoteServerController::refreshList()
{
    // 异常纠错记录轮询（供 ErrorCenterPage）
    QNetworkReply *e = m_rc->apiGet("/api/errors");
    connect(e, &QNetworkReply::finished, this, [this, e]() {
        if (e->error() == QNetworkReply::NoError) {
            const QVariantList recs = QJsonDocument::fromJson(e->readAll()).object()
                                          .value(QStringLiteral("errors")).toArray().toVariantList();
            if (recs != m_errorRecords) { m_errorRecords = recs; emit errorRecordsChanged(); }
        }
        e->deleteLater();
    });

    QNetworkReply *r = m_rc->apiGet("/api/servers");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            const QJsonArray arr = o.value("servers").toArray();
            QSet<QString> nowRunning;
            for (const QJsonValue &v : arr) {
                const QJsonObject s = v.toObject();
                const QString name = s.value("name").toString();
                const QString path = s.value("path").toString();
                const bool running = s.value("running").toBool();
                m_nameByPath[path] = name;
                if (running) {
                    nowRunning.insert(path);
                    // 拉取该服务器详情（console/properties/mods/players）
                    QNetworkReply *d = m_rc->apiGet("/api/servers/" + enc(name));
                    connect(d, &QNetworkReply::finished, this, [this, d, path]() {
                        if (d->error() == QNetworkReply::NoError) {
                            applyServerJson(QJsonDocument::fromJson(d->readAll()).object());
                        }
                        d->deleteLater();
                    });
                }
            }
            // 运行态变化 -> 发信号
            if (nowRunning != m_running) {
                QSet<QString> all = nowRunning | m_running;
                for (const QString &p : all) {
                    const bool was = m_running.contains(p);
                    const bool now = nowRunning.contains(p);
                    if (was != now)
                        emit stateChanged(m_nameByPath.value(p), now);
                }
                m_running = nowRunning;
                emit runningCountChanged();
            }
        }
        r->deleteLater();
    });
}

void RemoteServerController::applyServerJson(const QJsonObject &o)
{
    const QString path = o.value("path").toString();
    if (path.isEmpty()) return;
    m_detail[path] = o;
    emit playersChanged(m_nameByPath.value(path), players(path));
}

bool RemoteServerController::pathExists(const QString &path) const
{
    return m_detail.contains(path) || m_nameByPath.contains(path);
}

void RemoteServerController::start(const QString &name, const QString &path,
                                   const QString &javaPath, int minMem, int maxMem, bool resetRetry)
{
    Q_UNUSED(javaPath); Q_UNUSED(resetRetry);
    QNetworkReply *r = m_rc->apiGet("/api/servers/" + enc(name) + "/start",
                                    {{"min", minMem}, {"max", maxMem}});
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

void RemoteServerController::stop(const QString &path)
{
    const QString name = m_nameByPath.value(path);
    QNetworkReply *r = m_rc->apiGet("/api/servers/" + enc(name) + "/stop");
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

void RemoteServerController::forceStop(const QString &path)
{
    const QString name = m_nameByPath.value(path);
    QNetworkReply *r = m_rc->apiGet("/api/servers/" + enc(name) + "/forcestop");
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

void RemoteServerController::send(const QString &path, const QString &cmd)
{
    const QString name = m_nameByPath.value(path);
    QNetworkReply *r = m_rc->apiGet("/api/servers/" + enc(name) + "/send", {{"cmd", cmd}});
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

QString RemoteServerController::getConsole(const QString &path) const
{
    return m_detail.value(path).value("console").toString();
}

QStringList RemoteServerController::players(const QString &path) const
{
    QStringList out;
    const QJsonArray a = m_detail.value(path).value("players").toArray();
    for (const QJsonValue &v : a) out.append(v.toString());
    return out;
}

QStringList RemoteServerController::listMods(const QString &path) const
{
    QStringList out;
    const QJsonArray a = m_detail.value(path).value("mods").toArray();
    for (const QJsonValue &v : a) out.append(v.toString());
    return out;
}

QVariantMap RemoteServerController::readProperties(const QString &path)
{
    QVariantMap out;
    const QJsonObject p = m_detail.value(path).value("properties").toObject();
    for (auto it = p.begin(); it != p.end(); ++it)
        out.insert(it.key(), it.value().toVariant());
    return out;
}

void RemoteServerController::writeProperties(const QString &path, const QVariantMap &map)
{
    const QString name = m_nameByPath.value(path);
    QJsonObject body;
    for (auto it = map.begin(); it != map.end(); ++it)
        body.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QNetworkReply *r = m_rc->apiPost("/api/servers/" + enc(name) + "/properties", body);
    connect(r, &QNetworkReply::finished, this, [this, r, path]() {
        r->deleteLater();
        // 写回后刷新详情缓存
        QNetworkReply *d = m_rc->apiGet("/api/servers/" + enc(m_nameByPath.value(path)));
        connect(d, &QNetworkReply::finished, this, [this, d, path]() {
            if (d->error() == QNetworkReply::NoError) applyServerJson(QJsonDocument::fromJson(d->readAll()).object());
            d->deleteLater();
        });
    });
}

QVariantList RemoteServerController::runningServerUsages() const
{
    QVariantList out;
    for (const QString &path : m_running) {
        QVariantMap m;
        m[QStringLiteral("name")] = m_nameByPath.value(path);
        m[QStringLiteral("cpu")] = m_sysCpu;
        m[QStringLiteral("mem")] = m_sysMem;
        m[QStringLiteral("players")] = players(path).size();
        m[QStringLiteral("uptime")] = 0;
        out << m;
    }
    return out;
}

QVariantList RemoteServerController::runningServerNames() const
{
    QVariantList out;
    for (const QString &path : m_running) {
        const QString name = m_nameByPath.value(path);
        if (!name.isEmpty()) out.append(name);
    }
    return out;
}
void RemoteServerController::retryNow(const QString &path)
{
    QNetworkReply *r = m_rc->apiPost("/api/errors/" + enc(path) + "/retry", QJsonObject());
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}
void RemoteServerController::stopRetries(const QString &path)
{
    QNetworkReply *r = m_rc->apiPost("/api/errors/" + enc(path) + "/stop", QJsonObject());
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}
void RemoteServerController::clearError(const QString &path)
{
    QNetworkReply *r = m_rc->apiPost("/api/errors/" + enc(path) + "/clear", QJsonObject());
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

int RemoteServerController::serverPort(const QString &path) const
{
    const QJsonObject p = m_detail.value(path).value("properties").toObject();
    return p.value("server-port").toInt(25565);
}
bool RemoteServerController::isPortFree(int port) const
{
    QNetworkReply *r = m_rc->apiGet("/api/portfree", {{QStringLiteral("port"), port}});
    QEventLoop loop; QTimer t; t.setSingleShot(true);
    bool free = false;
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(r, &QNetworkReply::finished, &loop, [&]() {
        if (r->error() == QNetworkReply::NoError)
            free = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("free")).toBool();
        r->deleteLater(); loop.quit();
    });
    t.start(3000); loop.exec(); if (t.isActive()) t.stop();
    return free;
}
int RemoteServerController::assignFreePort(const QString &path)
{
    const int base = serverPort(path);
    QNetworkReply *r = m_rc->apiGet("/api/assignfreeport",
        {{QStringLiteral("from"), base + 1}, {QStringLiteral("to"), qMin(base + 500, 65535)}});
    QEventLoop loop; QTimer t; t.setSingleShot(true);
    int port = -1;
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(r, &QNetworkReply::finished, &loop, [&]() {
        if (r->error() == QNetworkReply::NoError)
            port = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("port")).toInt(-1);
        r->deleteLater(); loop.quit();
    });
    t.start(3000); loop.exec(); if (t.isActive()) t.stop();
    if (port > 0) {
        QVariantMap m; m.insert(QStringLiteral("server-port"), QString::number(port));
        writeProperties(path, m);
    }
    return port;
}
