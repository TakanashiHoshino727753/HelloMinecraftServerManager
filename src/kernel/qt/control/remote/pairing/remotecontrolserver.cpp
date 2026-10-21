#include "remotecontrolserver.h"
#include "settingscontroller.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QHostAddress>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QUuid>
#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QAbstractSocket>

static QString randomHex(int bytes)
{
    QString s;
    s.reserve(bytes * 2);
    for (int i = 0; i < bytes; ++i)
        s += QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(2, '0');
    return s;
}

static QString lanIp()
{
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
                return a.toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

RemoteControlServer::RemoteControlServer(SettingsController *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_port = 25577;   // 远控端独立端口（与 WebUI 的 25575 区分）
    m_exposeLan = m_settings->webuiExposeLan();
    m_webuiToken = m_settings->webuiToken();

    m_nam = new QNetworkAccessManager(this);
    m_server = new RemoteTcpServer(this);
    connect(m_server, &RemoteTcpServer::newDescriptor, this, &RemoteControlServer::handleDescriptor);

    m_pairTimer = new QTimer(this);
    m_pairTimer->setInterval(2000);
    connect(m_pairTimer, &QTimer::timeout, this, [this]() {
        if (m_pairing && QDateTime::currentMSecsSinceEpoch() > m_pairExpiresMs) {
            m_pairing = false; m_pairCode.clear(); m_pairUri.clear();
            emit pairCodeChanged(); emit pairUriChanged(); emit pairingChanged();
        }
    });
    m_pairTimer->start();

    connect(m_settings, &SettingsController::webuiTokenChanged, this, [this]() {
        m_webuiToken = m_settings->webuiToken();
    });

    loadDevices();
}

void RemoteControlServer::setRemotePort(int p)
{
    if (m_port == p) return;
    m_port = p;
    emit remotePortChanged();
    if (m_server->isListening()) { stop(); start(); }
}

void RemoteControlServer::setExposeLan(bool on)
{
    if (m_exposeLan == on) return;
    m_exposeLan = on;
    emit exposeLanChanged();
    if (m_server->isListening()) { stop(); start(); }
}

void RemoteControlServer::start()
{
    if (!m_server) return;
    loadTls();   // 复用 WebUI 自签证书启用 HTTPS（若可用）
    const QHostAddress addr = m_exposeLan ? QHostAddress::Any : QHostAddress::LocalHost;
    if (!m_server->listen(addr, m_port)) {
        qWarning() << "[RemoteControl] 监听失败:" << m_server->errorString()
                   << "地址=" << addr.toString() << "端口=" << m_port;
    } else {
        qInfo() << "[RemoteControl] 监听于" << (m_exposeLan ? "0.0.0.0" : "127.0.0.1")
                << ":" << m_port << (m_https ? "(HTTPS，配对/控制，独立于 WebUI)"
                                            : "(明文，独立 WebUI)");
    }
}

QString RemoteControlServer::certDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    dir += QStringLiteral("/webui");
    QDir().mkpath(dir);
    return dir;
}

void RemoteControlServer::loadTls()
{
    m_https = false;
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "[RemoteControl] 系统不支持 SSL（缺 TLS 后端插件），将以明文提供（配对码/令牌在局域网明文传输）";
        return;
    }
    const QString cerPath = certDir() + QStringLiteral("/hmcsm_webui.crt");
    const QString keyPath = certDir() + QStringLiteral("/hmcsm_webui.key");
    if (!QFile::exists(cerPath) || !QFile::exists(keyPath)) {
        qWarning() << "[RemoteControl] 未找到 WebUI 自签证书，将以明文提供（启动 WebUI 一次生成证书后即可启用 HTTPS）：" << cerPath;
        return;
    }
    const QList<QSslCertificate> certs = QSslCertificate::fromPath(cerPath);
    QFile kf(keyPath);
    if (certs.isEmpty() || !kf.open(QIODevice::ReadOnly)) {
        qWarning() << "[RemoteControl] 读取 WebUI 证书失败，将以明文提供";
        return;
    }
    const QSslKey key(&kf, QSsl::Rsa);
    if (key.isNull()) {
        qWarning() << "[RemoteControl] WebUI 私钥无效，将以明文提供";
        return;
    }
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);   // 服务端不校验客户端证书
    conf.setProtocol(QSsl::TlsV1_2OrLater);
    conf.setLocalCertificate(certs.first());
    conf.setPrivateKey(key);
    m_sslConf = conf;
    m_https = true;
    qInfo() << "[RemoteControl] 已加载 TLS 证书（复用 WebUI 自签），配对/控制将以 HTTPS 提供";
}

void RemoteControlServer::handleDescriptor(qintptr fd)
{
    if (m_https) {
        auto *ssl = new QSslSocket(this);
        if (!ssl->setSocketDescriptor(fd)) {
            qWarning() << "[RemoteControl] 无法接管 socket 描述符（HTTPS），关闭连接";
            ssl->deleteLater();
            return;
        }
        ssl->setSslConfiguration(m_sslConf);
        // 必须在握手【之前】连好 readyRead：localhost 上远控端常在握手完成的同一瞬间就把 HTTP 请求发来，
        // 若等到 encrypted() 里再连，请求会被永久挂起。
        connect(ssl, &QTcpSocket::readyRead, this, [this, ssl]() { onReadyRead(ssl); });
        connect(ssl, &QSslSocket::encrypted, this, [this, ssl]() {
            qInfo() << "[RemoteControl] TLS 握手完成 (fd=" << (int)ssl->socketDescriptor() << ")";
        });
        connect(ssl, &QTcpSocket::disconnected, ssl, &QObject::deleteLater);
        // 自签证书：忽略 sslErrors（peerVerifyMode 已设为 VerifyNone，这里兜底）
        connect(ssl, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                ssl, [ssl](const QList<QSslError> &) { ssl->ignoreSslErrors(); });
        ssl->startServerEncryption();
    } else {
        auto *sock = new QTcpSocket(this);
        if (!sock->setSocketDescriptor(fd)) {
            sock->deleteLater();
            return;
        }
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void RemoteControlServer::stop()
{
    if (m_server) m_server->close();
}

void RemoteControlServer::generatePairCode()
{
    const int code = 100000 + QRandomGenerator::global()->bounded(900000);
    m_pairCode = QString::number(code);
    m_pairUri = QStringLiteral("hmcsm-remote://%1@%2:%3?s=%4")
                    .arg(m_pairCode, localUriHost(), QString::number(m_port))
                    .arg(m_https ? 1 : 0);
    m_pairing = true;
    m_pairExpiresMs = QDateTime::currentMSecsSinceEpoch() + 120000;   // 2 分钟有效
    emit pairCodeChanged();
    emit pairUriChanged();
    emit pairingChanged();
    qInfo() << "[RemoteControl] 生成配对码，二维码 URI:" << m_pairUri;
}

void RemoteControlServer::revokeDevice(const QString &id)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_devices.removeAt(i);
            saveDevices();
            emit devicesChanged();
            return;
        }
    }
}

// ---------------- 设备存储 ----------------
QString RemoteControlServer::storePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/HMCSM");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/remote_devices.json");
}

void RemoteControlServer::loadDevices()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isArray())
        m_devices = doc.array().toVariantList();
}

void RemoteControlServer::saveDevices()
{
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonArray::fromVariantList(m_devices)).toJson(QJsonDocument::Indented));
    f.commit();
}

QString RemoteControlServer::localUriHost() const
{
    return m_exposeLan ? lanIp() : QStringLiteral("127.0.0.1");
}

// ---------------- 连接处理 ----------------
void RemoteControlServer::onReadyRead(QTcpSocket *s)
{
    m_buffers[s].append(s->readAll());
    const QByteArray &buf = m_buffers[s];
    const int idx = buf.indexOf("\r\n\r\n");
    if (idx < 0) return;   // 头未收完
    // Content-Length
    int cl = 0;
    const QByteArray head = buf.left(idx);
    const QByteArray body0 = buf.mid(idx + 4);
    QRegularExpression re("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(head);
    if (m.hasMatch()) cl = m.captured(1).toInt();
    if (body0.size() < cl) return;   // body 未收完
    const QByteArray raw = buf.left(idx + 4 + cl);
    m_buffers.remove(s);
    processRequest(s, raw);
}

void RemoteControlServer::processRequest(QTcpSocket *s, const QByteArray &raw)
{
    const int idx = raw.indexOf("\r\n\r\n");
    const QByteArray head = raw.left(idx);
    const QByteArray body = raw.mid(idx + 4);
    const QList<QByteArray> lines = head.split('\r');
    if (lines.isEmpty()) { writeResponse(s, 400, "text/plain", "bad request"); return; }
    const QList<QByteArray> reqLine = lines.first().split(' ');
    if (reqLine.size() < 2) { writeResponse(s, 400, "text/plain", "bad request"); return; }
    const QByteArray method = reqLine.at(0);
    const QByteArray full = reqLine.at(1);
    const int qpos = full.indexOf('?');
    const QByteArray path = qpos < 0 ? full : full.left(qpos);
    const QByteArray query = qpos < 0 ? QByteArray() : full.mid(qpos + 1);

    // 解析头
    QMap<QByteArray, QByteArray> hdr;
    for (int i = 1; i < lines.size(); ++i) {
        const int c = lines.at(i).indexOf(':');
        if (c > 0) hdr[lines.at(i).left(c).trimmed()] = lines.at(i).mid(c + 1).trimmed();
    }

    auto sendJson = [&](int code, const QJsonObject &o) {
        writeResponse(s, code, "application/json",
                      QJsonDocument(o).toJson(QJsonDocument::Compact));
    };

    const QString p = QString::fromUtf8(path);

    // —— 配对与控制路由 ——
    if (p == QStringLiteral("/remote/info")) {
        sendJson(200, QJsonObject{{QStringLiteral("requiresPairing"), m_devices.isEmpty()},
                                   {QStringLiteral("paired"), false},
                                   {QStringLiteral("port"), m_port}});
        return;
    }
    if (p == QStringLiteral("/remote/pair/start")) {
        generatePairCode();
        sendJson(200, QJsonObject{{QStringLiteral("code"), m_pairCode},
                                   {QStringLiteral("uri"), m_pairUri},
                                   {QStringLiteral("https"), m_https}});
        return;
    }
    if (p == QStringLiteral("/remote/pair/finish")) {
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QString code = o.value(QStringLiteral("code")).toString();
        if (!m_pairing || code != m_pairCode
            || QDateTime::currentMSecsSinceEpoch() > m_pairExpiresMs) {
            sendJson(400, QJsonObject{{QStringLiteral("error"), QStringLiteral("配对码无效或已过期")}});
            return;
        }
        QVariantMap dev;
        dev[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        dev[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
        dev[QStringLiteral("platform")] = o.value(QStringLiteral("platform")).toString();
        dev[QStringLiteral("token")] = randomHex(32);
        dev[QStringLiteral("createdMs")] = QDateTime::currentMSecsSinceEpoch();
        dev[QStringLiteral("lastSeenMs")] = dev[QStringLiteral("createdMs")];
        m_devices.append(dev);
        saveDevices();
        // 一次性：用后即废
        m_pairing = false; m_pairCode.clear(); m_pairUri.clear();
        emit pairCodeChanged(); emit pairUriChanged(); emit pairingChanged(); emit devicesChanged();
        sendJson(200, QJsonObject{{QStringLiteral("deviceToken"), dev[QStringLiteral("token")].toString()},
                                   {QStringLiteral("id"), dev[QStringLiteral("id")].toString()},
                                   {QStringLiteral("name"), dev[QStringLiteral("name")].toString()}});
        return;
    }

    // 其余需要设备令牌鉴权
    QString tok;
    const QByteArray auth = hdr.value("Authorization");
    if (auth.startsWith("Bearer ")) tok = QString::fromUtf8(auth.mid(7));
    else if (hdr.contains("X-HMCSM-Device")) tok = QString::fromUtf8(hdr.value("X-HMCSM-Device"));
    QVariantMap dev;
    if (tok.isEmpty() || !validDeviceToken(tok, dev)) {
        sendJson(401, QJsonObject{{QStringLiteral("error"), QStringLiteral("未配对或令牌无效")}});
        return;
    }
    // 记录活动并转发控制请求到本地 WebUIServer 回环
    dev[QStringLiteral("lastSeenMs")] = QDateTime::currentMSecsSinceEpoch();
    // 更新 lastSeen（就地）
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices.at(i).toMap().value(QStringLiteral("token")).toString() == tok)
            m_devices[i] = dev;
    saveDevices();
    emit devicesChanged();
    emit activity(dev[QStringLiteral("platform")].toString());

    if (p == QStringLiteral("/remote/status")) {
        sendJson(200, QJsonObject{{QStringLiteral("paired"), true},
                                   {QStringLiteral("device"), QJsonObject::fromVariantMap(dev)}});
        return;
    }
    if (p == QStringLiteral("/remote/unpair")) {
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        revokeDevice(o.value(QStringLiteral("id")).toString());
        sendJson(200, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (p.startsWith(QStringLiteral("/api/"))) {
        const QByteArray ct = hdr.value("Content-Type");
        proxyToWebui(s, QString::fromUtf8(method), p, query, body, ct);
        return;
    }
    sendJson(404, QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});
}

void RemoteControlServer::writeResponse(QTcpSocket *s, int code, const QByteArray &ct, const QByteArray &body)
{
    static const QHash<int, QByteArray> reason{
        {200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"}, {404, "Not Found"},
        {405, "Method Not Allowed"}, {500, "Internal Server Error"}, {502, "Bad Gateway"}};
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(code) + " " + reason.value(code, "OK") + "\r\n";
    resp += "Content-Type: " + ct + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    s->write(resp);
    s->disconnectFromHost();
}

bool RemoteControlServer::validDeviceToken(const QString &tok, QVariantMap &dev) const
{
    for (const QVariant &v : m_devices) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("token")).toString() == tok) { dev = m; return true; }
    }
    return false;
}

void RemoteControlServer::proxyToWebui(QTcpSocket *s, const QString &method, const QString &path,
                                       const QByteArray &query, const QByteArray &body, const QByteArray &ct)
{
    QUrl u;
    // WebUI 以 HTTPS 提供（自签证书），反代必须同样用 https，否则会得到 502 Bad Gateway
    u.setScheme(QStringLiteral("https"));
    u.setHost(QStringLiteral("127.0.0.1"));
    u.setPort(m_settings->webuiPort());
    u.setPath(path);
    u.setQuery(query.isEmpty() ? QString() : QString::fromUtf8(query));
    QNetworkRequest req(u);
    {
        QSslConfiguration c = QSslConfiguration::defaultConfiguration();
        c.setPeerVerifyMode(QSslSocket::VerifyNone);   // 自签证书：不校验
        c.setProtocol(QSsl::TlsV1_2OrLater);
        req.setSslConfiguration(c);
    }
    req.setRawHeader("Authorization", ("Bearer " + m_webuiToken).toUtf8());
    if (!ct.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, ct);

    QNetworkReply *r = nullptr;
    if (method == QStringLiteral("GET")) r = m_nam->get(req);
    else if (method == QStringLiteral("POST")) r = m_nam->post(req, body);
    else if (method == QStringLiteral("PUT")) r = m_nam->put(req, body);
    else if (method == QStringLiteral("DELETE")) r = m_nam->deleteResource(req);
    else { writeResponse(s, 405, "text/plain", "method not allowed"); return; }

    // 关键：远端断开时 socket 会被 deleteLater 销毁，按值捕获原始指针会在回调里悬垂解引用
    // （远端先退出 → 本地端 SIGSEGV）。改用 QPointer，对象销毁后自动置空，安全跳过。
    QPointer<QTcpSocket> ps = s;
    connect(r, &QNetworkReply::finished, this, [this, ps, r]() {
        if (!ps || ps->state() == QAbstractSocket::UnconnectedState) { r->deleteLater(); return; }
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray ct = r->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        const QByteArray data = r->readAll();
        r->deleteLater();
        writeResponse(ps, code > 0 ? code : 502, ct.isEmpty() ? "application/json" : ct, data);
    });
}
