// remoteclient.cpp —— 见 remoteclient.h
#include "remoteclient.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QRegularExpression>
#include <QSysInfo>
#include <QSettings>

// 将 QSysInfo 平台归一化为远控端分灯用的平台键（windows/linux/macos/android/ios）。
static QString remotePlatformKey()
{
    const QString pt = QSysInfo::productType();
    if (pt == QLatin1String("windows")) return QLatin1String("windows");
    if (pt == QLatin1String("osx") || pt == QLatin1String("macos") || pt == QLatin1String("darwin")) return QLatin1String("macos");
    if (pt == QLatin1String("ios"))     return QLatin1String("ios");
    if (pt == QLatin1String("android")) return QLatin1String("android");
    if (pt == QLatin1String("linux"))   return QLatin1String("linux");
    return QLatin1String("unknown");
}

RemoteClient::RemoteClient(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    // 自签证书：忽略证书错误（本地端用 openssl 自签，无法被系统信任）
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    QSslConfiguration::setDefaultConfiguration(ssl);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, this, &RemoteClient::refreshServers);

    m_optPollTimer = new QTimer(this);
    m_optPollTimer->setInterval(1500);
    connect(m_optPollTimer, &QTimer::timeout, this, [this]() {
        if (m_optName.isEmpty())
            return;
        QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(m_optName) + "/optimods");
        connect(r, &QNetworkReply::finished, this, [this, r]() {
            if (r->error() == QNetworkReply::NoError) {
                QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
                m_optMods = o.value("mods").toArray();
                m_optLoading = o.value("loading").toBool();
                emit optModsChanged();
                emit optLoadingChanged();
                if (!m_optLoading && !m_optMods.isEmpty())
                    m_optPollTimer->stop();   // 检索完成
            }
            r->deleteLater();
        });
    });
    loadStored();
    emit pairedChanged();
}

void RemoteClient::loadStored()
{
    QSettings s;
    m_deviceToken = s.value(QStringLiteral("remote/deviceToken")).toString();
    m_deviceId = s.value(QStringLiteral("remote/deviceId")).toString();
    m_deviceName = s.value(QStringLiteral("remote/deviceName")).toString();
    m_host = s.value(QStringLiteral("remote/host")).toString();
    m_port = s.value(QStringLiteral("remote/port"), 25577).toInt();
    m_https = s.value(QStringLiteral("remote/https"), false).toBool();
}

void RemoteClient::saveStored()
{
    QSettings s;
    s.setValue(QStringLiteral("remote/deviceToken"), m_deviceToken);
    s.setValue(QStringLiteral("remote/deviceId"), m_deviceId);
    s.setValue(QStringLiteral("remote/deviceName"), m_deviceName);
    s.setValue(QStringLiteral("remote/host"), m_host);
    s.setValue(QStringLiteral("remote/port"), m_port);
    s.setValue(QStringLiteral("remote/https"), m_https);
}

void RemoteClient::startPair(const QString &host, int port, bool https)
{
    m_pendingHost = host.trimmed();
    m_pendingPort = port;
    m_pendingHttps = https;
    QUrl url;
    url.setScheme(https ? "https" : "http");
    url.setHost(m_pendingHost);
    url.setPort(m_pendingPort);
    url.setPath(QStringLiteral("/remote/pair/start"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applySsl(req);
    QNetworkReply *r = m_nam->post(req, QByteArray());
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_pairCode = o.value(QStringLiteral("code")).toString();
            // 以服务端实际是否 HTTPS 为准（自签证书的 HTTPS 也能连）
            m_pendingHttps = o.value(QStringLiteral("https")).toBool();
            m_https = m_pendingHttps;
            emit httpsChanged();
            m_pairing = true;
            emit pairCodeChanged();
            emit pairingChanged();
            emit toast(tr("已获取配对码"), m_pairCode);
        } else {
            emit toast(tr("连接失败"), r->errorString());
        }
        r->deleteLater();
    });
}

void RemoteClient::pairWithUri(const QString &remoteUri, const QString &name)
{
    // hmcsm-remote://CODE@host:port
    QRegularExpression re("^hmcsm-remote://([^@]+)@([^:/?]+)(?::(\\d+))?(?:\\?s=(\\d))?$");
    QRegularExpressionMatch mt = re.match(remoteUri.trimmed());
    if (!mt.hasMatch()) {
        emit toast(tr("无法解析配对串"), remoteUri);
        return;
    }
    const QString code = mt.captured(1);
    const QString host = mt.captured(2);
    const int port = mt.captured(3).isEmpty() ? 25577 : mt.captured(3).toInt();
    const bool https = mt.captured(4) == QStringLiteral("1");
    finishPair(code, host, port, https, name);
}

void RemoteClient::finishPair(const QString &code, const QString &host, int port, bool https, const QString &name)
{
    QUrl url;
    url.setScheme(https ? "https" : "http");
    url.setHost(host.trimmed());
    url.setPort(port);
    url.setPath(QStringLiteral("/remote/pair/finish"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applySsl(req);
    const QString plat = remotePlatformKey();
    QJsonObject body{{QStringLiteral("code"), code.trimmed().toUpper()},
                     {QStringLiteral("name"), name},
                     {QStringLiteral("platform"), plat}};
    QNetworkReply *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, host, port, https, name]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_deviceToken = o.value(QStringLiteral("deviceToken")).toString();
            m_deviceId = o.value(QStringLiteral("id")).toString();
            m_deviceName = name;
            m_host = host; m_port = port; m_https = https;
            emit httpsChanged();
            saveStored();
            emit pairedChanged();
            emit deviceNameChanged();
            emit deviceIdChanged();
            m_pairing = false; emit pairingChanged(); m_pairCode.clear(); emit pairCodeChanged();
            emit toast(tr("配对成功"), name);
            connectTo(m_host, m_port, m_deviceToken, m_https);
        } else {
            emit toast(tr("配对失败"), r->errorString());
        }
        r->deleteLater();
    });
}

void RemoteClient::forgetPair()
{
    m_deviceToken.clear();
    m_deviceId.clear();
    m_deviceName.clear();
    m_pairCode.clear(); m_pairing = false;
    saveStored();
    emit pairedChanged();
    emit deviceNameChanged();
    emit deviceIdChanged();
    emit pairCodeChanged();
    emit pairingChanged();
    disconnectFrom();
}

void RemoteClient::tryAutoConnect()
{
    if (!m_deviceToken.isEmpty() && !m_host.isEmpty())
        connectTo(m_host, m_port, m_deviceToken, m_https);
}

void RemoteClient::setConnected(bool v)
{
    if (m_connected != v) {
        m_connected = v;
        emit connectedChanged();
    }
}

void RemoteClient::setError(const QString &e)
{
    if (m_lastError != e) {
        m_lastError = e;
        emit lastErrorChanged();
    }
}

QUrl RemoteClient::buildUrl(const QString &path, const QVariantMap &query) const
{
    QUrl url;
    url.setScheme(m_https ? "https" : "http");
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(path);
    QUrlQuery q;
    for (auto it = query.begin(); it != query.end(); ++it)
        q.addQueryItem(it.key(), it.value().toString());
    url.setQuery(q);
    return url;
}

// 将 QSysInfo 平台归一化为远控端分灯用的平台键（windows/linux/macos/android/ios）。
void RemoteClient::applyToken(QNetworkRequest &req)
{
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    // 标明朝向身份：远控端（区别于本地 Web 控制台），供 WebUI 在“远控端”大类下按平台分灯。
    const QString plat = remotePlatformKey();
    req.setRawHeader("X-HMCSM-Client", "remote");
    req.setRawHeader("X-HMCSM-Platform", plat.toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("HMCSM-Remote/%1 (%2; %3)")
                      .arg(plat, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
}

void RemoteClient::applySsl(QNetworkRequest &req)
{
    if (!m_https) return;
    QSslConfiguration conf = req.sslConfiguration();
    if (conf.isNull()) conf = QSslConfiguration::defaultConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);   // 自签证书：不校验服务端证书
    conf.setProtocol(QSsl::TlsV1_2OrLater);
    req.setSslConfiguration(conf);
}

QNetworkReply *RemoteClient::apiGet(const QString &path, const QVariantMap &query)
{
    QNetworkRequest req(buildUrl(path, query));
    applyToken(req);
    applySsl(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_nam->get(req);
}

QNetworkReply *RemoteClient::apiPost(const QString &path, const QJsonObject &body)
{
    QNetworkRequest req(buildUrl(path, {}));
    applyToken(req);
    applySsl(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return m_nam->post(req, data);
}

QNetworkReply *RemoteClient::apiDelete(const QString &path)
{
    QNetworkRequest req(buildUrl(path, {}));
    applyToken(req);
    applySsl(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_nam->deleteResource(req);
}

QNetworkReply *RemoteClient::apiPut(const QString &path, const QJsonObject &body)
{
    QNetworkRequest req(buildUrl(path, {}));
    applyToken(req);
    applySsl(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return m_nam->put(req, data);
}

void RemoteClient::connectTo(const QString &host, int port, const QString &token, bool https)
{
    m_host = host.trimmed();
    m_port = port;
    m_token = token.trimmed();
    m_https = https;
    emit hostChanged();
    emit portChanged();
    emit tokenChanged();
    emit httpsChanged();

    // 用 /api/servers 探活：成功即视为已连接
    QNetworkReply *r = apiGet("/api/servers");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            setConnected(true);
            setError(QString());
            refreshServers();
            emit toast(tr("已连接"), m_host + ":" + QString::number(m_port));
        } else {
            setConnected(false);
            setError(r->errorString());
            emit toast(tr("连接失败"), r->errorString());
        }
        r->deleteLater();
    });
}

void RemoteClient::connectFromUri(const QString &hmcsmUri)
{
    QVariantMap m;
    if (parseUri(hmcsmUri, m))
        connectTo(m.value("host").toString(), m.value("port").toInt(), m.value("token").toString(),
                  m.value("https", true).toBool());
    else
        setError(tr("无法解析连接串"));
}

bool RemoteClient::parseUri(const QString &hmcsmUri, QVariantMap &out)
{
    // 格式：hmcsm://token@host:port?t=token   或   hmcsm://host:port?t=token
    QRegularExpression re("^hmcsm://(?:([^@]+)@)?([^:/?]+)(?::(\\d+))?(?:\\?t=([^&]+))?$");
    QRegularExpressionMatch mt = re.match(hmcsmUri.trimmed());
    if (!mt.hasMatch())
        return false;
    out["token"] = mt.captured(1).isEmpty() ? mt.captured(4) : mt.captured(1);
    out["host"] = mt.captured(2);
    out["port"] = mt.captured(3).isEmpty() ? 25575 : mt.captured(3).toInt();
    out["https"] = true;
    return true;
}

QString RemoteClient::toHmcsmUri() const
{
    return QString("hmcsm://%1@%2:%3?t=%1").arg(m_token, m_host, QString::number(m_port));
}

void RemoteClient::disconnectFrom()
{
    m_pollTimer->stop();
    setConnected(false);
}

void RemoteClient::refreshServers()
{
    if (!m_connected)
        return;
    QNetworkReply *r = apiGet("/api/servers");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_servers = o.value("servers").toArray();
            emit serversChanged();
        }
        r->deleteLater();
    });
}

void RemoteClient::startServer(const QString &name, int min, int max)
{
    QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(name) + "/start",
                              {{"min", min}, {"max", max}});
    connect(r, &QNetworkReply::finished, this, [this, r, name]() {
        if (r->error() == QNetworkReply::NoError)
            emit toast(tr("启动"), name);
        else
            emit toast(tr("启动失败"), r->errorString());
        r->deleteLater();
        refreshServers();
    });
}

void RemoteClient::stopServer(const QString &name)
{
    QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(name) + "/stop");
    connect(r, &QNetworkReply::finished, this, [this, r, name]() {
        if (r->error() == QNetworkReply::NoError)
            emit toast(tr("停止"), name);
        else
            emit toast(tr("停止失败"), r->errorString());
        r->deleteLater();
        refreshServers();
    });
}

void RemoteClient::forceStopServer(const QString &name)
{
    QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(name) + "/forcestop");
    connect(r, &QNetworkReply::finished, this, [this, r, name]() {
        r->deleteLater();
        refreshServers();
    });
}

void RemoteClient::sendCommand(const QString &name, const QString &cmd)
{
    QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(name) + "/send",
                              {{"cmd", cmd}});
    connect(r, &QNetworkReply::finished, this, [r]() { r->deleteLater(); });
}

void RemoteClient::fetchOptimods(const QString &name, const QString &mc, const QString &loader)
{
    m_optName = name;
    m_optMods = QJsonArray();
    m_optLoading = true;
    emit optModsChanged();
    emit optLoadingChanged();
    // 触发检索
    QNetworkReply *r = apiGet("/api/servers/" + QUrl::toPercentEncoding(name) + "/optimods",
                              {{"mc", mc}, {"loader", loader}});
    connect(r, &QNetworkReply::finished, this, [this, r, name]() {
        r->deleteLater();
        if (r->error() == QNetworkReply::NoError)
            m_optPollTimer->start();   // 开始轮询直至 loading=false
        else
            emit toast(tr("优化模组检索失败"), r->errorString());
    });
}

void RemoteClient::installOptimod(const QString &name, const QString &mc, const QString &loader)
{
    QNetworkReply *r = apiPost("/api/servers/" + QUrl::toPercentEncoding(name) + "/installoptimod",
                               {{"mc", mc}, {"loader", loader}});
    connect(r, &QNetworkReply::finished, this, [this, r, name]() {
        if (r->error() == QNetworkReply::NoError)
            emit toast(tr("已发起优化模组安装"), name + " 将在本地端后台安装");
        else
            emit toast(tr("安装失败"), r->errorString());
        r->deleteLater();
    });
}
