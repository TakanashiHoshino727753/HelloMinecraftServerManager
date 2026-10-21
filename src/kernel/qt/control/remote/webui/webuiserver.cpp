/*
 * webuiserver.cpp —— 本地 Web 管理界面服务
 * -------------------------------------------------
 * 在 0.0.0.0:port 监听 HTTP，提供与本地端一致的单页管理面板（SPA 内嵌于
 * webuiserver_spa.inc）。按路由分发业务（服务器启停、下载、创建/导入、设置），
 * 以 JSON 接口与少量 HTML 文本回传；使用独立的 DownloadCatalog 实例与本地端隔离。
 */
#include "webuiserver.h"
#include "servermanager.h"
#include "servercontroller.h"
#include "downloadmanager.h"
#include "downloadlistmodel.h"
#include "downloadcatalog.h"
#include "createservercontroller.h"
#include "modpackimporter.h"
#include "settingscontroller.h"
#include "javamanager.h"
#include "botcontroller.h"
#include "installcoordinator.h"
#include "httpclient.h"
// 控制层依赖执行层：一律用「裸文件名 + CMake 暴露的 include 目录」引用，
// 不再写 ../xxx 相对路径（相对路径会随目录调整失效，也会掩盖真实的模块依赖）。
#include "proxymanager.h"
#include "backupcontroller.h"
#include "schedulercontroller.h"
#include "updatecontroller.h"
#include "portmapper.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJSEngine>
#include <QUrlQuery>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QColor>
#include <QMetaObject>
#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QDateTime>
#include "qrcode_lib.h"   // 内嵌 qrcode-generator，供配对页生成二维码
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincrypt.h>
#endif

// 内部子类：重写 incomingConnection，直接拿到原生 socket 描述符交给 QSslSocket 接管，
// 避免“先 nextPendingConnection 拿 QTcpSocket 再 setSocketDescriptor 接管”导致两个 QObject
// 共享同一 fd、互相抢事件（表现为 TLS 握手卡死/HTTP 无响应，在 Linux 上必现）。
class WebUIServer;
class WebTcpServer : public QTcpServer
{
public:
    explicit WebTcpServer(WebUIServer *hub, QObject *parent = nullptr)
        : QTcpServer(parent), m_hub(hub) {}
protected:
    void incomingConnection(qintptr socketDescriptor) override;
private:
    WebUIServer *m_hub;
};

// ---------------- HTTP 工具 ----------------
// urlDecode / parseQuery 统一复用 core/httpclient.h（HttpClient::urlDecode / HttpClient::parseQuery）。

void WebTcpServer::incomingConnection(qintptr socketDescriptor)
{
    m_hub->handleIncoming(socketDescriptor);
}

// ---------------- 构造 / 生命周期 ----------------

WebUIServer::WebUIServer(ServerManager *sm, ServerController *sc, DownloadManager *dm,
                         CreateServerController *create, ModpackImporter *import,
                         SettingsController *settings, QObject *monitor, JavaManager *java,
                         QObject *parent)
    : QObject(parent), m_sm(sm), m_sc(sc), m_create(create),
      m_import(import), m_settings(settings), m_monitor(monitor), m_java(java)
{
    // 必须用 WebTcpServer：它重写 incomingConnection()，把原生 socket 描述符直接交给
    // handleIncoming() 处理（明文建 QTcpSocket / HTTPS 建 QSslSocket 握手）。
    // 若用普通 QTcpServer，连接只会触发 newConnection → onNewConnection()（空实现），
    // 端口虽在监听却永远没人处理请求 —— 这正是「WebUI 打不开」的根因。
    // WebTcpServer 定义在本文件上方，构造时即可使用；不必再连 newConnection。
    m_server = new WebTcpServer(this, this);
    // WebUI 使用独立的下载目录实例，与本地端界面互不干扰（仍共用底层 DownloadManager 完成真实下载）
    m_dm = dm;   // 保存底层下载管理器，供 WebUI 暴露统一的“下载任务”列表
    m_webCatalog = new DownloadCatalog(dm, this);
    m_webCatalog->setJavaManager(m_java);
    // “下载并打包选中加载器”复用 CreateServerController（setSkipAddList=true），与本地端
    // “创建服务器”完全一致：准备临时 Java（架构自适应）+ 跑安装器 + 产出可直接运行的服务端目录。
    m_webCatalog->setPackager([this, dm, sm]() {
        auto *builder = new CreateServerController(dm, sm, m_java, m_webCatalog);
        QString parent = m_webCatalog->saveDir();
        if (parent.isEmpty()) parent = dm->defaultDownloadDir();
        QStringList labs;
        for (const QString &ld : m_webCatalog->selectedLoaders()) labs.append(m_webCatalog->loaderLabel(ld));
        const QString folder = QStringLiteral("Mod-") + m_webCatalog->modVersion() + QStringLiteral("-")
                               + labs.join(QStringLiteral("-"));
        const QString dest = QDir::cleanPath(QDir::fromNativeSeparators(parent) + QStringLiteral("/") + folder);
        // 顺序：trigger refreshSaveDir 的 setter 先设，最后 setSaveDir 固定产物目录
        builder->setCurrentType(QStringLiteral("Mod"));
        builder->setCurrentVersion(m_webCatalog->modVersion());
        builder->setSelectedLoaders(m_webCatalog->selectedLoaders());
    builder->setEulaAccepted(true);
    builder->setSkipAddList(true);   // 不加入服务器列表
    builder->setPackaged(true);      // 下载中心：模组服直接打包为压缩包
    builder->setSaveDir(dest);
        connect(builder, &CreateServerController::statusTextChanged, m_webCatalog, [this, builder]() {
            m_webCatalog->setStatus(builder->status());
        });
        connect(builder, &CreateServerController::busyChanged, builder, [this, builder]() {
            if (!builder->busy()) {
                if (m_java) m_java->cleanupTemp();
                builder->deleteLater();
            }
        });
        builder->create();
    });
    m_webCatalog->refresh();
    // 移动端连接状态轮询：每 3 秒判定近期是否有携带令牌的请求到达
    m_activityTimer = new QTimer(this);
    m_activityTimer->setInterval(3000);
    connect(m_activityTimer, &QTimer::timeout, this, &WebUIServer::evalMobileConnected);
    connect(m_activityTimer, &QTimer::timeout, this, &WebUIServer::evalRemoteConnected);
    m_activityTimer->start();
    // 设置中已启用则随启动自动开启（无需每次手动拨动开关）
    if (m_settings && m_settings->webuiEnabled())
        setEnabled(true);
}

void WebUIServer::setPort(int p)
{
    if (m_port == p) return;
    m_port = p;
    emit portChanged();
    if (m_enabled && m_server->isListening())
        rebind();
}

void WebUIServer::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    emit enabledChanged();
    if (on) {
        if (!startListen()) {
            m_error = m_server->errorString();
            emit errorChanged();
        } else {
            m_error.clear();
            emit errorChanged();
            emit runningChanged();
        }
    } else {
        // 关闭 WebUI 前台，但控制通道(回环)保持监听，供“远控端”反代使用（远控功能不依赖 WebUI 前台）
        rebind();
        emit runningChanged();
    }
}

// 重新监听（端口/暴露范围变化时），使用最新的 TLS 与绑定设置
void WebUIServer::rebind()
{
    if (m_server) m_server->close();
    if (!startListen()) {
        m_error = m_server->errorString();
        emit errorChanged();
    } else {
        m_error.clear();
        emit errorChanged();
    }
    emit runningChanged();
}

// 配置 TLS 并开始监听。暴露到局域网(0.0.0.0)时必须带令牌；默认仅本机(127.0.0.1)。
bool WebUIServer::startListen()
{
    // 优先同步加载已有证书（快，不阻塞）；不存在/无效时同步生成（保证 HTTPS，
    // 避免“证书未就绪→明文→用户用 https 访问得到空白页”）。仅首次/证书损坏时占用至多数秒。
    if (!loadExistingTls())
        ensureCertSync();
    // 启用且暴露局域网 → 0.0.0.0；否则始终监听回环(127.0.0.1)，保证控制通道/远控反代可用
    const QHostAddress addr = (m_enabled && m_settings->webuiExposeLan())
        ? QHostAddress::Any
        : QHostAddress::LocalHost;
    // 始终用普通 QTcpServer 接收连接；HTTPS 时在 handleIncoming 里手动把
    // 原生描述符接管进 QSslSocket 并 startServerEncryption()（而非用 QSslServer）。
    // 原因：QSslServer 在某些 Qt 6 版本存在 readyRead 不转发/响应不刷出的问题，
    // 表现为 TLS 握手成功但 HTTP 无响应（空白页）。手动接管模式稳定可靠。
    if (!m_server) {
        m_server = new WebTcpServer(this, this);
        // WebTcpServer 已重写 incomingConnection 直接接管描述符，无需 newConnection 信号
    }
    const bool ok = m_server->listen(addr, m_port);
    if (!ok) {
        // 监听失败必须留日志：否则用户只看到“打不开”而无从判断（端口占用 / 地址不可用等）
        qWarning() << "[WebUI] 监听失败:" << m_server->errorString()
                   << "地址=" << addr.toString() << "端口=" << m_port;
    } else {
        qInfo() << "[WebUI] 监听于" << (m_settings->webuiExposeLan() ? "0.0.0.0" : addr.toString())
                << ":" << m_port << (m_https ? "(HTTPS)" : "(明文)");
    }
    return ok;
}

void WebUIServer::setThemeState(bool dark, const QColor &accent)
{
    m_dark = dark;
    m_accent = accent;
}

// ---------------- 连接处理 ----------------

void WebUIServer::onNewConnection()
{
    // 已改为 WebTcpServer::incomingConnection 直接接管描述符，这里保留为空接口。
    // 明文模式在 handleIncoming 内直接处理。
    Q_UNUSED(m_server);
}

void WebUIServer::handleIncoming(qintptr socketDescriptor)
{
    if (!m_https) {
        // 明文模式：直接用原生 socket 描述符创建 QTcpSocket
        auto *sock = new QTcpSocket(this);
        if (!sock->setSocketDescriptor(socketDescriptor)) {
            sock->deleteLater();
            return;
        }
        connect(sock, &QTcpSocket::readyRead, this, &WebUIServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &WebUIServer::onDisconnected);
        return;
    }
    // HTTPS 模式：直接用原生描述符创建 QSslSocket 并握手，不经过中间 QTcpSocket，
    // 避免两个 QObject 共享 fd 抢事件导致握手卡死。
    auto *ssl = new QSslSocket(this);
    if (!ssl->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "[WebUI] 无法接管 socket 描述符，关闭连接";
        ssl->deleteLater();
        return;
    }
    ssl->setSslConfiguration(m_sslConf);
    // 必须在握手【之前】连好 readyRead：localhost 上浏览器常在 TLS 握手完成的同一瞬间
    // 就把 HTTP 请求发来；若等到 encrypted() 里再连接，这段空窗期到达的解密数据会进入
    // QSslSocket 缓冲却无人读取（Qt 不会重放 readyRead），请求被永久挂起 → 页面空白。
    connect(ssl, &QTcpSocket::readyRead, this, &WebUIServer::onReadyRead);
    connect(ssl, &QSslSocket::encrypted, this, [this, ssl]() {
        qInfo() << "[WebUI] TLS 握手完成 (fd=" << (int)ssl->socketDescriptor() << ")";
    });
    connect(ssl, &QTcpSocket::disconnected, this, &WebUIServer::onDisconnected);
    connect(ssl, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, [ssl](const QList<QSslError> &) {
                // 自签证书：忽略证书错误，继续握手（浏览器侧用 -k / 信任例外）
                ssl->ignoreSslErrors();
            });
    ssl->startServerEncryption();
    qInfo() << "[WebUI] 新 HTTPS 连接，开始 TLS 握手 (fd=" << (int)socketDescriptor << ")";
}

void WebUIServer::onDisconnected()
{
    QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
    if (s) m_buffers.remove(s);
}

// 手动解析 HTTP/1.0 请求：累积收到的字节，直到出现空行（头结束）并凑齐
// Content-Length 指定的 body，再一次性分发；未收齐时直接返回并等待后续 readyRead。
// 每个连接的状态保存在 m_buffers，连接断开时由 onDisconnected 清理。
void WebUIServer::onReadyRead()
{
    QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
    if (!s) return;
    m_buffers[s].append(s->readAll());

    const int headerEnd = m_buffers[s].indexOf("\r\n\r\n");
    if (headerEnd < 0) return; // 等待完整头部

    const QByteArray head = m_buffers[s].left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) return;
    const QByteArray first = lines.at(0).trimmed();
    const int sp1 = first.indexOf(' ');
    const int sp2 = first.lastIndexOf(' ');
    if (sp1 < 0 || sp2 < 0 || sp2 <= sp1) return;
    const QString method = QString::fromLatin1(first.left(sp1));
    const QString fullPath = QString::fromLatin1(first.mid(sp1 + 1, sp2 - sp1 - 1));

    int cl = 0;
    for (const QByteArray &l : lines) {
        if (QString::fromUtf8(l).startsWith(QLatin1String("Content-Length:"), Qt::CaseInsensitive))
            cl = l.mid(15).trimmed().toInt();
    }
    const int bodyStart = headerEnd + 4;
    if (m_buffers[s].size() < bodyStart + cl) return; // 等待完整 body
    const QByteArray body = m_buffers[s].mid(bodyStart, cl);
    m_buffers[s].clear();

    // 拆分 path 与 query 字符串（query 为 '?' 之后部分，不含 '?'）
    QString path = fullPath, query;
    const int qidx = fullPath.indexOf(QLatin1Char('?'));
    if (qidx >= 0) {
        path = fullPath.left(qidx);
        query = fullPath.mid(qidx + 1);
    }

    // 解析请求头（用于令牌校验）
    QMap<QString, QString> hdr;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray l = lines.at(i).trimmed();
        if (l.isEmpty()) continue;
        const int cpos = l.indexOf(':');
        if (cpos < 0) continue;
        hdr[QString::fromUtf8(l.left(cpos)).toLower()] =
            QString::fromUtf8(l.mid(cpos + 1)).trimmed();
    }

    QElapsedTimer rt; rt.start();
    dispatch(method, path, query, hdr, body, s);
    const qint64 ms = rt.elapsed();
    if (ms > 200)
        qWarning() << "[WebUI] 请求处理慢:" << ms << "ms" << method << path;
}

// ---------------- 响应辅助 ----------------
// 以下 send* 统一写出 HTTP 头（短连接：Connection: close，请求-响应后即断开），
// 分别以 JSON / HTML / 纯文本 / 状态码回传；调用后均 disconnectFromHost。
void WebUIServer::sendJson(QTcpSocket *sock, const QJsonObject &obj, int code)
{
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n"
                      "Content-Length: " + QByteArray::number(data.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + data;
    sock->write(resp);
    sock->disconnectFromHost();
}

void WebUIServer::sendHtml(QTcpSocket *sock, const QString &html)
{
    const QByteArray data = html.toUtf8();
    QByteArray resp = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html; charset=utf-8\r\n"
                      "Content-Length: " + QByteArray::number(data.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + data;
    sock->write(resp);
    sock->disconnectFromHost();
}

void WebUIServer::sendText(QTcpSocket *sock, const QString &text, const QString &ct)
{
    const QByteArray data = text.toUtf8();
    QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: " + ct.toUtf8() +
                      "\r\nContent-Length: " + QByteArray::number(data.size()) +
                      "\r\nConnection: close\r\n\r\n" + data;
    sock->write(resp);
    sock->disconnectFromHost();
}

void WebUIServer::sendStatus(QTcpSocket *sock, int code, const QString &msg)
{
    const QByteArray data = msg.toUtf8();
    QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + " \r\n"
                      "Content-Type: text/plain; charset=utf-8\r\n"
                      "Content-Length: " + QByteArray::number(data.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + data;
    sock->write(resp);
    sock->disconnectFromHost();
}

// ---------------- 安全：TLS 与令牌 ----------------

QString WebUIServer::certDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    dir += QStringLiteral("/webui");
    QDir().mkpath(dir);
    return dir;
}

// 全平台统一：用 openssl 命令生成自签证书(crt) + 私钥(key) 两个 PEM 文件，
// 10 年有效，主题 CN=localhost，并带 subjectAltName（localhost + 127.0.0.1）。
// 不再使用 Windows CryptoAPI 自签：CertCreateSelfSignCertificate 在部分 Windows 版本
// 会触发 0xc0000005 访问违规（导致整进程 SIGSEGV 闪退），且其导出的 PKCS#8 私钥与
// schannel/openssl 后端兼容性差。openssl 生成的 PEM 是业界标准格式，所有 TLS 后端通用。
//
// openssl 可执行文件的查找顺序：① 程序同目录（Windows 部署时随 OpenSSL 一起带上）；
// ② 系统 PATH。本机常见位置：Git for Windows 的 mingw64/bin（开箱即用）。
#include <QProcess>
static QString findOpenSsl()
{
    const QString exeName =
#ifdef Q_OS_WIN
        QStringLiteral("openssl.exe");
#else
        QStringLiteral("openssl");
#endif
    // 1) 程序同目录
    const QString dir = QCoreApplication::applicationDirPath();
    const QString local = dir + QLatin1Char('/') + exeName;
    if (QFile::exists(local))
        return local;
    // 2) 系统 PATH（Qt 的 QStandardPaths 找不到时退给 QProcess 自己解析）
    const QString envPath = qEnvironmentVariable("PATH");
    const QChar sep =
#ifdef Q_OS_WIN
        QLatin1Char(';');
#else
        QLatin1Char(':');
#endif
    for (const QString &p : envPath.split(sep, Qt::SkipEmptyParts)) {
        const QString cand = p + QLatin1Char('/') + exeName;
        if (QFile::exists(cand))
            return cand;
    }
    // 3) 退回 PATH 解析（由 QProcess 自行查找）
    return exeName;
}

static bool generateSelfSignedCertFiles(const QString &certPath, const QString &keyPath)
{
    const QString openssl = findOpenSsl();
    QProcess proc;
    // OpenSSL 启动时会去硬编码路径找 openssl.cnf（如 Git 版相对运行位置的 etc/ssl/，
    // 部署目录下并不存在），找不到会直接失败。设 OPENSSL_CONF 指向空设备即可，
    // req 自签不需要完整配置。Windows 用 NUL，类 Unix 用 /dev/null。
    proc.setProcessEnvironment([&]() {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OPENSSL_CONF"),
#ifdef Q_OS_WIN
                   QStringLiteral("NUL")
#else
                   QStringLiteral("/dev/null")
#endif
        );
        return env;
    }());
    proc.setProgram(openssl);
    proc.setArguments({
        QStringLiteral("req"), QStringLiteral("-x509"),
        QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
        QStringLiteral("-nodes"),
        QStringLiteral("-keyout"), keyPath,
        QStringLiteral("-out"), certPath,
        QStringLiteral("-days"), QStringLiteral("3650"),
        QStringLiteral("-subj"), QStringLiteral("/CN=localhost"),
        QStringLiteral("-addext"), QStringLiteral("subjectAltName=DNS:localhost,IP:127.0.0.1"),
    });
    proc.start();
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(30000)) {
        qWarning() << "[WebUI] openssl 启动/执行超时（可能 openssl.exe 不在 PATH 或部署目录）";
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        qWarning() << "[WebUI] openssl 生成自签证书失败:" << proc.readAllStandardError();
        return false;
    }
    return QFile::exists(certPath) && QFile::exists(keyPath);
}

// 只同步加载“已存在且有效”的证书（快路径，不阻塞事件循环）。
// 返回 true 表示已可启用 HTTPS；false 表示需要（异步）生成或回退明文。
bool WebUIServer::loadExistingTls()
{
    m_https = false;
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "[WebUI] 系统不支持 SSL（缺少 TLS 后端插件），将以明文提供";
        return false;
    }
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);   // 服务端不校验客户端证书
    conf.setProtocol(QSsl::TlsV1_2OrLater);

    // 1) 用户自定义证书（PEM 证书 + 私钥）
    const QString certP = m_settings->webuiCertPath();
    const QString keyP = m_settings->webuiKeyPath();
    if (!certP.isEmpty() && !keyP.isEmpty() && QFile::exists(certP) && QFile::exists(keyP)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromPath(certP);
        QFile kf(keyP);
        if (!certs.isEmpty() && kf.open(QIODevice::ReadOnly)) {
            QSslKey key(&kf, QSsl::Rsa);
            if (!key.isNull()) {
                conf.setLocalCertificate(certs.first());
                conf.setPrivateKey(key);
                m_sslConf = conf;
                m_https = true;
                return true;
            }
        }
        qWarning() << "[WebUI] 自定义证书无效，回退自签证书";
    }

    // 2) 自动自签证书（生成过的会直接复用，避免每次启动都重跑 openssl 阻塞）
    const QString cerPath = certDir() + QStringLiteral("/hmcsm_webui.crt");
    const QString keyPath = certDir() + QStringLiteral("/hmcsm_webui.key");
    if (QFile::exists(cerPath) && QFile::exists(keyPath)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromPath(cerPath);
        QFile kf(keyPath);
        if (!certs.isEmpty() && kf.open(QIODevice::ReadOnly)) {
            QSslKey key(&kf, QSsl::Rsa);
            if (!key.isNull()) {
                conf.setLocalCertificate(certs.first());
                conf.setPrivateKey(key);
                m_sslConf = conf;
                m_https = true;
                return true;
            }
        }
        qWarning() << "[WebUI] 已存在的自签证书无效，将重新生成";
    }
    return false;
}

// 同步生成自签证书并加载（保证 HTTPS 可用，避免“证书未就绪→明文→用户用 https 访问得空白页”）。
// 仅在证书文件不存在/无效时调用，故仅在首次启动或证书损坏时占用至多数秒；之后走 loadExistingTls 快路径。
// 生成失败则返回 false（回退明文模式，功能不受影响，仅非加密）。
bool WebUIServer::ensureCertSync()
{
    const QString cerPath = certDir() + QStringLiteral("/hmcsm_webui.crt");
    const QString keyPath = certDir() + QStringLiteral("/hmcsm_webui.key");
    QFile::remove(cerPath);
    QFile::remove(keyPath);
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "[WebUI] 缺少 TLS 后端插件，无法生成证书，将以明文提供";
        return false;
    }
    if (!generateSelfSignedCertFiles(cerPath, keyPath)) {
        qWarning() << "[WebUI] 自签证书生成失败，将以明文提供（功能不受影响，仅非加密）";
        return false;
    }
    // 加载刚生成的证书
    if (!QFile::exists(cerPath) || !QFile::exists(keyPath))
        return false;
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    conf.setProtocol(QSsl::TlsV1_2OrLater);
    const QList<QSslCertificate> certs = QSslCertificate::fromPath(cerPath);
    QFile kf(keyPath);
    if (certs.isEmpty() || !kf.open(QIODevice::ReadOnly))
        return false;
    QSslKey key(&kf, QSsl::Rsa);
    if (key.isNull())
        return false;
    conf.setLocalCertificate(certs.first());
    conf.setPrivateKey(key);
    m_sslConf = conf;
    m_https = true;
    return true;
}

// 校验访问令牌：来源 Authorization: Bearer <token> 或 ?token=<token>。
// 无令牌设置时放行（理论上不会发生，构造时已自动生成）。
bool WebUIServer::checkToken(const QMap<QString, QString> &hdr, const QMap<QString, QString> &q) const
{
    const QString tok = m_settings->webuiToken();
    if (tok.isEmpty())
        return true;

    QString provided = hdr.value(QStringLiteral("authorization")).trimmed();
    if (provided.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        provided = provided.mid(7).trimmed();
    if (provided.isEmpty())
        provided = q.value(QStringLiteral("token")).trimmed();
    if (provided.isEmpty())
        return false;

    // 以 SHA-256 比较，避免明文比较的时序差异
    return QCryptographicHash::hash(provided.toUtf8(), QCryptographicHash::Sha256)
        == QCryptographicHash::hash(tok.toUtf8(), QCryptographicHash::Sha256);
}

// ---------------- 载荷构造 ----------------

QJsonObject WebUIServer::themePayload() const
{
    QJsonObject o;
    o[QStringLiteral("dark")] = m_dark;
    o[QStringLiteral("accent")] = m_accent.name();
    return o;
}

QJsonObject WebUIServer::settingsPayload() const
{
    QJsonObject o;
    o[QStringLiteral("language")] = m_settings->language();
    o[QStringLiteral("autoStart")] = m_settings->autoStart();
    o[QStringLiteral("defaultServerDir")] = m_settings->defaultServerDir();
    // 注意：webuiEnabled / webuiPort 对 WebUI 自身有害，不暴露
    return o;
}

QJsonObject WebUIServer::statePayload()
{
    QJsonObject o;
    o[QStringLiteral("theme")] = themePayload();
    o[QStringLiteral("settings")] = settingsPayload();
    QJsonObject sys;
    sys[QStringLiteral("cpu")] = m_monitor ? m_monitor->property("cpuUsage").toDouble() : 0;
    sys[QStringLiteral("mem")] = m_monitor ? m_monitor->property("memoryUsage").toDouble() : 0;
    // 注意：监控侧栏只用 system(cpu/mem)。此处不再返回 servers，避免监控轮询（每 3 秒）
    // 重复触发 serverListWithRunning()；前端监控改用轻量 /api/system 端点（见 dispatch 的 /system 路由）。
    o[QStringLiteral("system")] = sys;
    return o;
}

// 仅返回系统监控数据（CPU/内存），供侧栏监控轮询使用。
// 与 statePayload 拆开，避免监控刷新每次都跑 serverListWithRunning() 这类较重的计算。
QJsonObject WebUIServer::systemPayload()
{
    QJsonObject o;
    o[QStringLiteral("cpu")] = m_monitor ? m_monitor->property("cpuUsage").toDouble() : 0;
    o[QStringLiteral("mem")] = m_monitor ? m_monitor->property("memoryUsage").toDouble() : 0;
    return o;
}

QJsonArray WebUIServer::serverListWithRunning() const
{
    QJsonArray arr;
    for (const QVariant &v : m_sm->serverSummary()) {
        QJsonObject o = QJsonObject::fromVariantMap(v.toMap());
        o[QStringLiteral("running")] = m_sc->isRunning(o[QStringLiteral("name")].toString());
        arr.append(o);
    }
    return arr;
}

// ---------------- 路由 ----------------

// 路由分发：按 method + path 匹配业务端点并返回 JSON/HTML/状态；非 /api 路径返回 404，
// 根路径返回内嵌 SPA。所有响应均为短连接（见 send*）。
void WebUIServer::setBotController(BotController *bot)
{
    m_bot = bot;
}

void WebUIServer::setDownloadCatalog(DownloadCatalog *dc)
{
    m_dc = dc;
    if (m_dc) {
        // 优化模组检索结果缓存：信号到达即写入，供 WebUI 轮询 /api/optimods 读取
        connect(m_dc, &DownloadCatalog::optimizationModsReady, this,
                [this](const QVariantList &mods) { m_optModsCache = mods; });
    }
}

void WebUIServer::setUpdateController(UpdateController *c)
{
    m_update = c;
    if (!c) return;
    QObject::connect(c, &UpdateController::updateAvailable, this,
        [this](const QString &name, const QString &current, const QString &latest, const QString &url) {
            m_updateInfo = {{QStringLiteral("event"), QStringLiteral("available")},
                            {QStringLiteral("name"), name}, {QStringLiteral("current"), current},
                            {QStringLiteral("latest"), latest}, {QStringLiteral("url"), url}};
        });
    QObject::connect(c, &UpdateController::updateFinished, this,
        [this](const QString &name, bool ok, const QString &msg, const QString &newVersion) {
            m_updateInfo = {{QStringLiteral("event"), QStringLiteral("finished")},
                            {QStringLiteral("name"), name}, {QStringLiteral("ok"), ok},
                            {QStringLiteral("message"), msg}, {QStringLiteral("newVersion"), newVersion}};
        });
}

// ---------------- 移动端配对 ----------------

// 生成一次性配对码（格式 ABC-1234），刷新并重置使用状态；配对码 10 分钟内有效。
QString WebUIServer::generatePairCode()
{
    const QString chars = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ23456789"); // 去掉易混字符
    QString code;
    for (int i = 0; i < 7; ++i) {
        if (i == 3) code += QLatin1Char('-');
        else code += chars.at(QRandomGenerator::global()->bounded(chars.size()));
    }
    m_pairCode = code;
    m_pairUsed = false;
    m_pairGenMs = QDateTime::currentMSecsSinceEpoch();
    // 重新生成配对码即视为“作废旧配对”：立即使已连设备离线，避免“未配对但已连接”
    // （旧设备仍持有效 token 持续活动）。配对完成后 paired 与 mobileConnected 会再次置位，
    // 届时状态自然回到“配对且连接”。
    if (m_mobileConnected) {
        m_mobileConnected = false;
        emit mobileConnectedChanged();
    }
    emit pairedChanged();
    qInfo() << "[WebUI] 生成移动端配对码:" << code;
    return code;
}

// 返回配对 URI（hmcsm://token@host:port?t=...），手机扫码后解析出 host/port/token 直连。
QString WebUIServer::pairUri() const
{
    const QString tok = m_settings->webuiToken();
    QString host = QStringLiteral("127.0.0.1");
    // 暴露到 LAN 时给出本机首选非回环 IP，方便手机同网段直连
    if (m_settings->webuiExposeLan()) {
        const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
        for (const QHostAddress &a : addrs) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                host = a.toString();
                break;
            }
        }
    }
    return QStringLiteral("hmcsm://token@%1:%2?t=%3").arg(host).arg(m_port).arg(tok);
}

void WebUIServer::markMobileActivity()
{
    m_lastMobileActivityMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_mobileConnected) {
        m_mobileConnected = true;
        emit mobileConnectedChanged();
    }
}

void WebUIServer::evalMobileConnected()
{
    const bool connected = (QDateTime::currentMSecsSinceEpoch() - m_lastMobileActivityMs) < 30000;
    if (connected != m_mobileConnected) {
        m_mobileConnected = connected;
        emit mobileConnectedChanged();
    }
}

void WebUIServer::markRemoteActivity(const QString &platform)
{
    if (!platform.isEmpty())
        m_lastRemoteActivityMsByPlatform[platform] = QDateTime::currentMSecsSinceEpoch();
    recomputeRemote();
}

void WebUIServer::evalRemoteConnected()
{
    recomputeRemote();
}

// 依据各平台最近活动时刻（30s 窗口）重算 remoteConnected 与各平台状态，仅在变化时发信号。
void WebUIServer::recomputeRemote()
{
    static const QString platforms[5] = {
        QStringLiteral("windows"), QStringLiteral("linux"), QStringLiteral("macos"),
        QStringLiteral("android"), QStringLiteral("ios")
    };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantMap m;
    bool any = false;
    for (const QString &p : platforms) {
        const bool c = (now - m_lastRemoteActivityMsByPlatform.value(p, 0)) < 30000;
        m[p] = c;
        any = any || c;
    }
    if (m != m_remoteClients || any != m_remoteConnected) {
        m_remoteClients = m;
        m_remoteConnected = any;
        emit remoteClientsChanged();
        emit remoteConnectedChanged();
    }
}

// 服务端生成二维码：复用桌面端 QrImageProvider 的同一份 QRCODE_JS（经 QJSEngine 求值），
// 直接产出 SVG 的 data URI，避免 WebUI 配对页依赖浏览器端 JS 库（此前因 TLS 子资源竞态
// / 内联脚本问题导致 window.qrcode 未定义、二维码无法生成）。
QString WebUIServer::generateQrSvg(const QString &data) const
{
    if (data.isEmpty())
        return QString();
    QJSEngine eng;
    eng.globalObject().setProperty("__qrdata", data);
    // 与 QrImageProvider 一致：必须同一次 evaluate 内加载库并调用；固定版本 10 / 纠错 M。
    const QString js = QStringLiteral(
        "(function(){"
        "%1"
        "  var qr = qrcode(10, 'M');"
        "  qr.addData(__qrdata);"
        "  qr.make();"
        "  var n = qr.getModuleCount();"
        "  var rows = [];"
        "  for (var r = 0; r < n; r++) { var row = []; for (var c = 0; c < n; c++) { row.push(qr.isDark(r, c) ? 1 : 0); } rows.push(row); }"
        "  return JSON.stringify({ n: n, rows: rows });"
        "})()")
        .arg(QLatin1String(QRCODE_JS));
    const QJSValue res = eng.evaluate(js);
    if (res.isError()) {
        qWarning() << "[WebUI] 生成二维码失败:" << res.toString();
        return QString();
    }
    const QJsonObject obj = QJsonDocument::fromJson(res.toString().toUtf8()).object();
    const int n = obj.value(QStringLiteral("n")).toInt();
    const QJsonArray rows = obj.value(QStringLiteral("rows")).toArray();
    if (n <= 0 || rows.size() != n)
        return QString();
    const int cell = 4, margin = cell * 2;
    const int size = n * cell + margin * 2;
    QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%1\" viewBox=\"0 0 %1 %1\"><rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>").arg(size);
    for (int r = 0; r < n; ++r) {
        const QJsonArray row = rows.at(r).toArray();
        for (int c = 0; c < n; ++c) {
            if (row.at(c).toInt() == 1)
                svg += QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%3\" fill=\"#000\"/>")
                       .arg(margin + c * cell).arg(margin + r * cell).arg(cell);
        }
    }
    svg += QStringLiteral("</svg>");
    return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(svg.toUtf8().toBase64());
}

void WebUIServer::dispatch(const QString &method, const QString &path, const QString &query,
                           const QMap<QString, QString> &hdr, const QByteArray &body, QTcpSocket *sock)
{
    QElapsedTimer t; t.start();
    if (path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
        if (!m_enabled) { sendStatus(sock, 404, QStringLiteral("WebUI disabled")); return; }
        sendHtml(sock, m_spaHtml());
        const qint64 ms = t.elapsed();
        if (ms > 200) qWarning() << "[WebUI] SPA 响应慢:" << ms << "ms";
        return;
    }
    if (path == QStringLiteral("/favicon.ico")) { sendStatus(sock, 204, QString()); return; }

    // 公开静态资源：内嵌的 qrcode 库（配对页生成二维码用，无需令牌）
    if (path == QStringLiteral("/qrcode.js")) {
        sendText(sock, QString::fromUtf8(QRCODE_JS),
                 QStringLiteral("application/javascript; charset=utf-8"));
        return;
    }

    // 移动端配对端点：免令牌校验（否则手机永远拿不到令牌）。配对码换取真实 token。
    if (path == QStringLiteral("/api/pair") && method == QStringLiteral("POST")) {
        QJsonObject bj;
        if (!body.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument d = QJsonDocument::fromJson(body, &err);
            if (d.isObject()) bj = d.object();
        }
        const QString code = bj.value(QStringLiteral("code")).toString().trimmed().toUpper();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool expired = (now - m_pairGenMs) > 10 * 60 * 1000; // 10 分钟过期
        if (m_pairCode.isEmpty() || code != m_pairCode || m_pairUsed || expired) {
            qWarning() << "[WebUI] 配对失败: code=" << code
                       << "used=" << m_pairUsed << "expired=" << expired;
            sendStatus(sock, 403, QStringLiteral("Invalid or expired pair code"));
            return;
        }
        m_pairUsed = true; // 一次性：用后即废
        markMobileActivity();
        emit pairedChanged();
        sendJson(sock, QJsonObject{
            {QStringLiteral("token"), m_settings->webuiToken()},
            {QStringLiteral("port"), m_port},
            {QStringLiteral("https"), m_https}
        });
        return;
    }

    if (!path.startsWith(QStringLiteral("/api/"))) { sendStatus(sock, 404, QStringLiteral("Not Found")); return; }

    // 令牌校验：所有 /api/* 必须携带正确令牌，否则 401（SPA 与远程调用皆同）
    if (!checkToken(hdr, HttpClient::parseQuery(query))) {
        sendStatus(sock, 401, QStringLiteral("Unauthorized"));
        return;
    }
    // 通过令牌校验即记为一次活动：远控端（携带 X-HMCSM-Client: remote）与移动端共用同一令牌，
    // 按标头分别判定：远控端按 X-HMCSM-Platform 记录各平台活动，移动端记通用活动。
    if (hdr.value(QStringLiteral("x-hmcsm-client")).toLower() == QStringLiteral("remote"))
        markRemoteActivity(hdr.value(QStringLiteral("x-hmcsm-platform")).toLower());
    else
        markMobileActivity();

    // query 已从 path 剥离，由 onReadyRead 解析后传入，避免参数被丢弃
    const QMap<QString, QString> q = HttpClient::parseQuery(query);

    QJsonObject bodyJson;
    if (!body.isEmpty()) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (doc.isObject()) bodyJson = doc.object();
    }

    const QString api = path.mid(4); // 去掉 "/api"

    // 移动端配对信息（需鉴权）：返回当前配对码、配对 URI、剩余有效期秒数，供 WebUI 配对页展示。
    if (api == QStringLiteral("/paircode") && method == QStringLiteral("GET")) {
        if (m_pairCode.isEmpty())
            generatePairCode(); // 首次访问自动生成一个，避免用户找不到入口
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int remainSec = int((10 * 60 * 1000 - (now - m_pairGenMs)) / 1000);
        sendJson(sock, QJsonObject{
            {QStringLiteral("code"), m_pairCode},
            {QStringLiteral("uri"), pairUri()},
            {QStringLiteral("qr"), generateQrSvg(pairUri())},
            {QStringLiteral("remainSec"), remainSec > 0 ? remainSec : 0},
            {QStringLiteral("used"), m_pairUsed}
        });
        return;
    }

    // 服务器集合
    if (api == QStringLiteral("/servers") && method == QStringLiteral("GET")) {
        sendJson(sock, QJsonObject{{QStringLiteral("servers"), serverListWithRunning()}});
        return;
    }
    // 单个服务器
    if (api.startsWith(QStringLiteral("/servers/"))) {
        QString rest = api.mid(9);
        const int slash = rest.indexOf(QLatin1Char('/'));
        const QString name = HttpClient::urlDecode(slash < 0 ? rest : rest.left(slash));
        const QString action = slash < 0 ? QString() : rest.mid(slash + 1);
        Server *srv = m_sm->serverByName(name);
        if (!srv) { sendStatus(sock, 404, QStringLiteral("server not found")); return; }

        if (action.isEmpty()) {
            if (method == QStringLiteral("DELETE")) {
                // 运行中的服务器不允许删除：必须先停止（与本地端删除校验一致）
                if (m_sc->isRunning(name)) {
                    sendStatus(sock, 409, QStringLiteral("server is running, stop it first"));
                    return;
                }
                // 按服务器 name 在列表中定位索引后删除（与本地端删除服务器一致）
                const QVariantList summary = m_sm->serverSummary();
                int idx = -1;
                for (int i = 0; i < summary.size(); ++i) {
                    if (summary.at(i).toMap().value(QStringLiteral("name")).toString() == name) { idx = i; break; }
                }
                if (idx < 0) { sendStatus(sock, 404, QStringLiteral("server not found")); return; }
                m_sm->removeServer(idx);
                sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
                return;
            }
            if (method != QStringLiteral("GET")) { sendStatus(sock, 405, QString()); return; }
            QJsonObject o;
            o[QStringLiteral("name")] = srv->name();
            o[QStringLiteral("version")] = srv->version();
            o[QStringLiteral("type")] = srv->type();
            o[QStringLiteral("path")] = srv->path();
            o[QStringLiteral("running")] = m_sc->isRunning(name);
            o[QStringLiteral("players")] = QJsonArray::fromStringList(m_sc->players(name));
            o[QStringLiteral("console")] = m_sc->getConsole(name);
            o[QStringLiteral("properties")] = QJsonObject::fromVariantMap(m_sc->readProperties(srv->path()));
            o[QStringLiteral("mods")] = QJsonArray::fromStringList(m_sc->listMods(srv->path()));
            sendJson(sock, o);
            return;
        }
        if (action == QStringLiteral("start")) {
            const int min = q.value(QStringLiteral("min")).toInt();
            const int max = q.value(QStringLiteral("max")).toInt();
            // 在 ServerController 所在（主）线程执行，确保 stateChanged 信号从主线程发出，
            // 可靠投递到本地端 QML；否则跨线程直呼信号可能丢失，导致本地端状态不切换。
            QMetaObject::invokeMethod(m_sc, "start", Qt::QueuedConnection,
                Q_ARG(QString, name), Q_ARG(QString, srv->path()),
                Q_ARG(QString, QString()), Q_ARG(int, min > 0 ? min : 1024), Q_ARG(int, max > 0 ? max : 2048));
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (action == QStringLiteral("stop")) {
            QMetaObject::invokeMethod(m_sc, "stop", Qt::QueuedConnection, Q_ARG(QString, name));
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (action == QStringLiteral("forcestop")) {
            QMetaObject::invokeMethod(m_sc, "forceStop", Qt::QueuedConnection, Q_ARG(QString, name));
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (action == QStringLiteral("send")) {
            QMetaObject::invokeMethod(m_sc, "send", Qt::QueuedConnection, Q_ARG(QString, name), Q_ARG(QString, q.value(QStringLiteral("cmd"))));
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (action == QStringLiteral("console")) {
            sendJson(sock, QJsonObject{{QStringLiteral("console"), m_sc->getConsole(name)}});
            return;
        }
        if (action == QStringLiteral("players")) {
            sendJson(sock, QJsonObject{{QStringLiteral("players"), QJsonArray::fromStringList(m_sc->players(name))}});
            return;
        }
        if (action == QStringLiteral("properties") && method == QStringLiteral("POST")) {
            QVariantMap map;
            for (auto it = bodyJson.begin(); it != bodyJson.end(); ++it)
                map.insert(it.key(), it.value().toVariant());
            m_sc->writeProperties(srv->path(), map);
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        sendStatus(sock, 404, QStringLiteral("unknown action"));
        return;
    }

    // 下载中心
    if (api == QStringLiteral("/catalog") && method == QStringLiteral("GET")) {
        // 注意：仅在 /catalog/refresh 端点切换当前分类/类型；此处轮询只读取状态，
        // 不得再 setCurrentKey，否则会在刷新异步加载前误判“已完成”导致轮询提前停止、分类切不动。
        sendJson(sock, QJsonObject{
            {QStringLiteral("items"), QJsonArray::fromVariantList(m_webCatalog->items())},
            {QStringLiteral("currentKey"), m_webCatalog->currentKey()},
            {QStringLiteral("serverType"), m_webCatalog->serverType()},
            {QStringLiteral("saveDir"), m_webCatalog->saveDir()},
            {QStringLiteral("status"), m_webCatalog->status()},
            {QStringLiteral("loading"), m_webCatalog->loading()},
            // 模组服多加载器状态
            {QStringLiteral("mcReleases"), QJsonArray::fromStringList(m_webCatalog->mcReleases())},
            {QStringLiteral("modVersion"), m_webCatalog->modVersion()},
            {QStringLiteral("modLoaders"), QJsonArray::fromStringList(m_webCatalog->modLoaders())},
            {QStringLiteral("selectedLoaders"), QJsonArray::fromStringList(m_webCatalog->selectedLoaders())}
        });
        return;
    }
    if (api == QStringLiteral("/catalog/refresh") && method == QStringLiteral("GET")) {
        const QString cat = q.value(QStringLiteral("cat"));
        const QString type = q.value(QStringLiteral("type"));
        if (!cat.isEmpty()) m_webCatalog->setCurrentKey(cat);
        if (!type.isEmpty()) m_webCatalog->setServerType(type);
        m_webCatalog->refresh();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/catalog/download") && method == QStringLiteral("GET")) {
        const int idx = q.value(QStringLiteral("index")).toInt();
        m_webCatalog->download(idx);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/catalog/savedir") && method == QStringLiteral("GET")) {
        const QString dir = q.value(QStringLiteral("dir"));
        if (!dir.isEmpty()) m_webCatalog->setSaveDir(dir);
        sendJson(sock, QJsonObject{{QStringLiteral("saveDir"), m_webCatalog->saveDir()}});
        return;
    }
    if (api == QStringLiteral("/catalog/modversion") && method == QStringLiteral("GET")) {
        const QString v = q.value(QStringLiteral("version"));
        if (!v.isEmpty()) m_webCatalog->setModVersion(v);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/catalog/modloader") && method == QStringLiteral("GET")) {
        const QString l = q.value(QStringLiteral("loader"));
        if (!l.isEmpty()) m_webCatalog->toggleLoader(l);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/catalog/downloadselected") && method == QStringLiteral("GET")) {
        m_webCatalog->downloadSelectedLoaders();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    // 下载模组服安装器（与本地端“下载安装器”按钮一致）
    if (api == QStringLiteral("/catalog/downloadloader") && method == QStringLiteral("GET")) {
        m_webCatalog->downloadLoader(q.value(QStringLiteral("loader")));
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    // 下载列表项的暂停 / 继续 / 取消（与本地端下载列表一致）
    if ((api == QStringLiteral("/catalog/pause") || api == QStringLiteral("/catalog/resume")
         || api == QStringLiteral("/catalog/cancel")) && method == QStringLiteral("GET")) {
        bool ok = false;
        const int idx = q.value(QStringLiteral("index")).toInt(&ok);
        if (api == QStringLiteral("/catalog/pause")) m_webCatalog->pause(idx);
        else if (api == QStringLiteral("/catalog/resume")) m_webCatalog->resume(idx);
        else m_webCatalog->cancel(idx);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/catalog/cleantempjava") && method == QStringLiteral("GET")) {
        m_webCatalog->cleanupTempJava();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 统一安装 API：服务器类型 + 版本 + 加载器，可多个任务并发（供 WebUI / 机器人调用）。
    // POST 体：{"type":"paper|vanilla|mod","version":"1.21.1","loaders":["forge"],"label":"xxx","addToList":true}
    // 返回：{"taskId":"<uuid>"}
    if (api == QStringLiteral("/installserver") && method == QStringLiteral("POST")) {
        if (!m_install) { sendStatus(sock, 503, QStringLiteral("install coordinator not ready")); return; }
        const QString type = bodyJson.value(QStringLiteral("type")).toString();
        const QString version = bodyJson.value(QStringLiteral("version")).toString();
        const QString label = bodyJson.value(QStringLiteral("label")).toString();
        const bool addToList = bodyJson.value(QStringLiteral("addToList")).toBool(true);
        QStringList loaders;
        const QJsonValue lv = bodyJson.value(QStringLiteral("loaders"));
        if (lv.isArray()) {
            for (const QJsonValue &v : lv.toArray()) loaders.append(v.toString());
        }
        const QString id = m_install->installServer(type, version, loaders, label, addToList);
        if (id.isEmpty()) {
            sendStatus(sock, 400, QStringLiteral("invalid type/version"));
            return;
        }
        sendJson(sock, QJsonObject{{QStringLiteral("taskId"), id}});
        return;
    }
    // 安装任务进度查询（轮询快照）
    if (api == QStringLiteral("/installtasks") && method == QStringLiteral("GET")) {
        if (!m_install) { sendStatus(sock, 503, QStringLiteral("install coordinator not ready")); return; }
        sendJson(sock, QJsonObject{{QStringLiteral("tasks"), m_install->tasks()}});
        return;
    }
    // 清除已完成的安装任务（DELETE /api/installtasks）
    if (api == QStringLiteral("/installtasks") && method == QStringLiteral("DELETE")) {
        if (!m_install) { sendStatus(sock, 503, QStringLiteral("install coordinator not ready")); return; }
        m_install->clearFinished();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    // 统一下载任务列表（与本地端“下载任务”面板同源）：GET 拉取，按 row 操作
    if (api == QStringLiteral("/downloads") && method == QStringLiteral("GET")) {
        DownloadListModel *dl = m_dm ? m_dm->downloadList() : nullptr;
        sendJson(sock, QJsonObject{{QStringLiteral("items"), QJsonArray::fromVariantList(dl ? dl->items() : QVariantList())}});
        return;
    }
    if ((api == QStringLiteral("/downloads/pause") || api == QStringLiteral("/downloads/resume")
         || api == QStringLiteral("/downloads/cancel") || api == QStringLiteral("/downloads/restart")
         || api == QStringLiteral("/downloads/remove")) && method == QStringLiteral("GET")) {
        bool ok = false;
        const int row = q.value(QStringLiteral("row")).toInt(&ok);
        DownloadListModel *dl = m_dm ? m_dm->downloadList() : nullptr;
        if (dl) {
            if (api == QStringLiteral("/downloads/pause")) dl->pauseAt(row);
            else if (api == QStringLiteral("/downloads/resume")) dl->resumeAt(row);
            else if (api == QStringLiteral("/downloads/cancel")) dl->cancelAt(row);
            else if (api == QStringLiteral("/downloads/restart")) dl->restartAt(row);
            else dl->removeAt(row);
        }
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/downloads/clear") && method == QStringLiteral("GET")) {
        DownloadListModel *dl = m_dm ? m_dm->downloadList() : nullptr;
        if (dl) dl->clearFinished();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 创建服务器
    if (api == QStringLiteral("/createtypes") && method == QStringLiteral("GET")) {
        QJsonArray a;
        for (const QString &t : m_create->types()) a.append(t);
        sendJson(sock, QJsonObject{{QStringLiteral("types"), a}});
        return;
    }
    if (api == QStringLiteral("/versions") && method == QStringLiteral("GET")) {
        QJsonArray a;
        for (const QString &v : m_create->versions()) a.append(v);
        sendJson(sock, QJsonObject{{QStringLiteral("versions"), a},
                                   {QStringLiteral("type"), m_create->currentType()}});
        return;
    }
    if (api == QStringLiteral("/create/loadversions") && method == QStringLiteral("GET")) {
        const QString type = q.value(QStringLiteral("type"));
        if (!type.isEmpty()) m_create->setCurrentType(type);
        m_create->loadVersions();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/create") && method == QStringLiteral("POST")) {
        // 从压缩包导入模式：复用 CreateServerController::importZip，跳过全新下载流程
        if (bodyJson.contains(QStringLiteral("importMode")) && bodyJson.value(QStringLiteral("importMode")).toBool()) {
            m_create->importZip(bodyJson.value(QStringLiteral("zipPath")).toString());
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (bodyJson.contains(QStringLiteral("type"))) m_create->setCurrentType(bodyJson.value(QStringLiteral("type")).toString());
        // 内存分配（MB）：记录到 CreateServerController（与本地端内存字段一致）
        if (bodyJson.contains(QStringLiteral("memory"))) { const int mem = bodyJson.value(QStringLiteral("memory")).toInt(); m_create->setMinMemory(mem); m_create->setMaxMemory(mem); }
        if (bodyJson.contains(QStringLiteral("version"))) m_create->setCurrentVersion(bodyJson.value(QStringLiteral("version")).toString());
        if (bodyJson.contains(QStringLiteral("name"))) m_create->setName(bodyJson.value(QStringLiteral("name")).toString());
        if (bodyJson.contains(QStringLiteral("saveDir"))) m_create->setSaveDir(bodyJson.value(QStringLiteral("saveDir")).toString());
        if (bodyJson.contains(QStringLiteral("eulaAccepted"))) m_create->setEulaAccepted(bodyJson.value(QStringLiteral("eulaAccepted")).toBool());
        if (bodyJson.contains(QStringLiteral("selectedLoaders"))) {
            QList<QString> loaders;
            for (const QJsonValue &v : bodyJson.value(QStringLiteral("selectedLoaders")).toArray())
                loaders << v.toString();
            m_create->setSelectedLoaders(loaders);
        }
        m_create->create();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/create/progress") && method == QStringLiteral("GET")) {
        QJsonObject o;
        o[QStringLiteral("busy")] = m_create->busy();
        o[QStringLiteral("done")] = m_create->done();
        o[QStringLiteral("status")] = m_create->statusText();
        o[QStringLiteral("progress")] = m_create->progress();
        o[QStringLiteral("currentType")] = m_create->currentType();
        o[QStringLiteral("currentVersion")] = m_create->currentVersion();
        o[QStringLiteral("name")] = m_create->name();
        o[QStringLiteral("saveDir")] = m_create->saveDir();

        // Java 信息
        const QString cv = m_create->currentVersion();
        if (!cv.isEmpty()) {
            const int feature = JavaManager::requiredFeature(cv);
            const QString jp = m_java->javaPathFor(cv);
            // 返回结构化字段，由前端根据全局语言用 T() 组合文案
            o[QStringLiteral("java")] = feature;
            o[QStringLiteral("javaFound")] = !jp.isEmpty();
            if (!jp.isEmpty())
                o[QStringLiteral("javaPath")] = QDir::toNativeSeparators(jp);
        }

        // 模组加载器（仅 mod 类型）
        if (m_create->currentType() == QStringLiteral("mod")) {
            QJsonArray loaders;
            for (const QString &l : m_create->modLoaders()) {
                QJsonObject lo;
                lo[QStringLiteral("key")] = l;
                lo[QStringLiteral("label")] = m_create->loaderLabel(l);
                lo[QStringLiteral("compatible")] = m_create->loaderCompatible(l, cv);
                lo[QStringLiteral("selected")] = m_create->selectedLoaders().contains(l);
                loaders.append(lo);
            }
            o[QStringLiteral("modLoaders")] = loaders;
        }

        // 版本列表
        QJsonArray a;
        for (const QString &v : m_create->versions()) a.append(v);
        o[QStringLiteral("versions")] = a;

        sendJson(sock, o);
        return;
    }

    // 创建服务器重置（清除当前填写态，供远控端“重置”按钮）
    if (api == QStringLiteral("/create/reset") && method == QStringLiteral("POST")) {
        m_create->reset();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 导入整合包
    if (api == QStringLiteral("/import") && method == QStringLiteral("POST")) {
        if (bodyJson.contains(QStringLiteral("zipPath"))) m_import->setZipPath(bodyJson.value(QStringLiteral("zipPath")).toString());
        if (bodyJson.contains(QStringLiteral("targetDir"))) m_import->setTargetDir(bodyJson.value(QStringLiteral("targetDir")).toString());
        m_import->import();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/import/progress") && method == QStringLiteral("GET")) {
        sendJson(sock, QJsonObject{
            {QStringLiteral("busy"), m_import->busy()},
            {QStringLiteral("done"), m_import->done()},
            {QStringLiteral("status"), m_import->statusText()},
            {QStringLiteral("progress"), m_import->progress()},
            {QStringLiteral("modpackName"), m_import->modpackName()},
            {QStringLiteral("detectedType"), m_import->detectedType()},
            {QStringLiteral("loader"), m_import->loader()},
            {QStringLiteral("gameVersion"), m_import->gameVersion()}
        });
        return;
    }

    // 导入整合包重置（清除当前进度/缓存，供远控端“重置”按钮）
    if (api == QStringLiteral("/import/reset") && method == QStringLiteral("POST")) {
        m_import->reset();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 设置（排除 webui 开关/端口）
    if (api == QStringLiteral("/settings") && method == QStringLiteral("GET")) {
        sendJson(sock, settingsPayload());
        return;
    }
    if (api == QStringLiteral("/settings") && method == QStringLiteral("PUT")) {
        // 远控端：遍历所有设置键，分发到对应 setter（镜像桌面端 apply() 的表驱动写入）。
        // 外观透明度类字段归 AppearanceSettings(m_app)，其余归 SettingsController(m_settings)。
        for (auto it = bodyJson.constBegin(); it != bodyJson.constEnd(); ++it) {
            const QString k = it.key();
            const QJsonValue v = it.value();
            if (k == QStringLiteral("language")) m_settings->setLanguage(v.toString());
            else if (k == QStringLiteral("autoStart")) m_settings->setAutoStart(v.toBool());
            else if (k == QStringLiteral("defaultServerDir")) m_settings->setDefaultServerDir(v.toString());
            else if (k == QStringLiteral("showOnStartup")) m_settings->setShowOnStartup(v.toBool());
            else if (k == QStringLiteral("bgmEnabled")) m_settings->setBgmEnabled(v.toBool());
            else if (k == QStringLiteral("bgmVolume")) m_settings->setBgmVolume(v.toDouble());
            else if (k == QStringLiteral("bgImagePath")) m_settings->setBgImagePath(v.toString());
            else if (k == QStringLiteral("bgImageFolder")) m_settings->setBgImageFolder(v.toString());
            else if (k == QStringLiteral("bgImageMode")) m_settings->setBgImageMode(v.toString());
            else if (k == QStringLiteral("bgImageInterval")) m_settings->setBgImageInterval(v.toInt());
            else if (k == QStringLiteral("bgImageIntervalUnit")) m_settings->setBgImageIntervalUnit(v.toString());
            else if (k == QStringLiteral("bgImageIndex")) m_settings->setBgImageIndex(v.toInt());
            else if (k == QStringLiteral("webhookUrl")) m_settings->setWebhookUrl(v.toString());
            else if (k == QStringLiteral("webhookType")) m_settings->setWebhookType(v.toString());
            else if (k == QStringLiteral("webhookEnabled")) m_settings->setWebhookEnabled(v.toBool());
            else if (k == QStringLiteral("webhookCrash")) m_settings->setWebhookCrash(v.toBool());
            else if (k == QStringLiteral("webhookState")) m_settings->setWebhookState(v.toBool());
            else if (k == QStringLiteral("webhookPlayer")) m_settings->setWebhookPlayer(v.toBool());
            else if (k == QStringLiteral("napcat")) m_settings->setNapcatEnabled(v.toBool());
            else if (k == QStringLiteral("napcatPath")) m_settings->setNapcatPath(v.toString());
            else if (k == QStringLiteral("nonebot")) m_settings->setNonebotEnabled(v.toBool());
            else if (k == QStringLiteral("nonebotDir")) m_settings->setNonebotDir(v.toString());
            else if (k == QStringLiteral("botUsageInterval")) m_settings->setBotUsageInterval(v.toInt());
            else if (k == QStringLiteral("botLinkedStart")) m_settings->setBotLinkedStart(v.toBool());
            else if (k == QStringLiteral("botEnabled")) m_settings->setBotEnabled(v.toBool());
            else if (k == QStringLiteral("windowTitle")) m_settings->setWindowTitle(v.toString());
            else if (k == QStringLiteral("titleImagePath")) m_settings->setTitleImagePath(v.toString());
            else if (k == QStringLiteral("titleLayout")) m_settings->setTitleLayout(v.toString());
            // 注：bgLayerOpacity / bgScrimOpacity / uiTransparency 属远控客户端本地外观偏好，不上送被控端；
            //     bgImageList / titleImageUrl 由专门接口管理，此处忽略
        }
        m_settings->apply();
        sendJson(sock, settingsPayload());
        return;
    }

    // 主题
    if (api == QStringLiteral("/theme") && method == QStringLiteral("GET")) {
        sendJson(sock, themePayload());
        return;
    }
    if (api == QStringLiteral("/theme") && method == QStringLiteral("PUT")) {
        const bool dark = bodyJson.value(QStringLiteral("dark")).toBool();
        const QColor accent = QColor(bodyJson.value(QStringLiteral("accent")).toString());
        m_dark = dark;
        if (accent.isValid()) m_accent = accent;
        emit themeChangeRequested(dark, m_accent);
        sendJson(sock, themePayload());
        return;
    }

    // 本地插件（QQ 机器人）控制与状态
    if (api == QStringLiteral("/bot") && method == QStringLiteral("GET")) {
        // 设置项与本地端"QQ 机器人"卡完全一致；运行状态指 NapCat/NoneBot 程序进程本身
        QJsonObject o;
        o[QStringLiteral("botEnabled")] = m_settings->botEnabled();
        o[QStringLiteral("botLinkedStart")] = m_settings->botLinkedStart();
        o[QStringLiteral("napcatPath")] = m_settings->napcatPath();
        o[QStringLiteral("nonebotDir")] = m_settings->nonebotDir();
        o[QStringLiteral("usageInterval")] = m_settings->botUsageInterval();
        // 状态为字符串：running / starting / external / waiting / stopped
        o[QStringLiteral("napcatState")] = m_bot ? m_bot->napcatState() : QStringLiteral("stopped");
        o[QStringLiteral("nonebotState")] = m_bot ? m_bot->nonebotState() : QStringLiteral("stopped");
        o[QStringLiteral("hmcsmPluginState")] = m_bot ? m_bot->hmcsmPluginState() : QStringLiteral("unknown");
        sendJson(sock, o);
        return;
    }
    if (api == QStringLiteral("/bot") && method == QStringLiteral("POST")) {
        if (bodyJson.contains(QStringLiteral("enabled")))
            m_settings->setBotEnabled(bodyJson.value(QStringLiteral("enabled")).toBool());
        if (bodyJson.contains(QStringLiteral("linkedStart")))
            m_settings->setBotLinkedStart(bodyJson.value(QStringLiteral("linkedStart")).toBool());
        if (bodyJson.contains(QStringLiteral("napcatPath")))
            m_settings->setNapcatPath(bodyJson.value(QStringLiteral("napcatPath")).toString());
        if (bodyJson.contains(QStringLiteral("nonebotDir")))
            m_settings->setNonebotDir(bodyJson.value(QStringLiteral("nonebotDir")).toString());
        if (bodyJson.contains(QStringLiteral("usageInterval")))
            m_settings->setBotUsageInterval(bodyJson.value(QStringLiteral("usageInterval")).toInt());
        m_settings->apply();   // 持久化到注册表/配置
        QJsonObject o;
        o[QStringLiteral("ok")] = true;
        o[QStringLiteral("botEnabled")] = m_settings->botEnabled();
        o[QStringLiteral("botLinkedStart")] = m_settings->botLinkedStart();
        sendJson(sock, o);
        return;
    }

    // 远控端：向 QQ 推送一条消息（scope=all/admin）
    if (api == QStringLiteral("/bot/notify") && method == QStringLiteral("POST")) {
        if (m_bot) m_bot->notify(bodyJson.value(QStringLiteral("message")).toString(),
                                  bodyJson.value(QStringLiteral("scope")).toString());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    // 远控端：检测 NapCat 候选路径（供设置页下拉 / 自动检测）
    if (api == QStringLiteral("/bot/detect/napcat") && method == QStringLiteral("GET")) {
        QJsonObject o;
        if (m_bot) {
            QJsonArray arr;
            for (const QString &p : m_bot->detectNapcatPaths()) arr.append(p);
            o[QStringLiteral("paths")] = arr;
            o[QStringLiteral("path")] = m_bot->detectNapcatPath();
        }
        sendJson(sock, o);
        return;
    }
    // 远控端：检测 NoneBot 候选目录
    if (api == QStringLiteral("/bot/detect/nonebot") && method == QStringLiteral("GET")) {
        QJsonObject o;
        if (m_bot) {
            QJsonArray arr;
            for (const QString &p : m_bot->detectNonebotDirs()) arr.append(p);
            o[QStringLiteral("dirs")] = arr;
            o[QStringLiteral("dir")] = m_bot->detectNonebotDir();
        }
        sendJson(sock, o);
        return;
    }
    // 远控端：把 HMCSM 控制插件装进所选 NoneBot 目录
    if (api == QStringLiteral("/bot/plugin/install") && method == QStringLiteral("POST")) {
        const bool ok = m_bot ? m_bot->installHmcsmPlugin() : false;
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), ok}});
        return;
    }
    // 远控端：用户在“手动安装后重试”时调用，重新检测并启动 NoneBot
    if (api == QStringLiteral("/bot/retry") && method == QStringLiteral("POST")) {
        if (m_bot) m_bot->retryStartNonebot();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // Java
    if (api == QStringLiteral("/java") && method == QStringLiteral("GET")) {
        const QString mc = q.value(QStringLiteral("mc"));
        sendJson(sock, QJsonObject::fromVariantMap(m_java->statusFor(mc)));
        return;
    }
    if (api == QStringLiteral("/java/download") && method == QStringLiteral("GET")) {
        const QString mc = q.value(QStringLiteral("mc"));
        m_java->ensure(mc, [](bool, const QString &) {});
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("downloading"), true}});
        return;
    }
    // 设置手动 Java 目录（与本地端 Java 路径选择一致）
    if (api == QStringLiteral("/java/sethome") && method == QStringLiteral("GET")) {
        const QString dir = q.value(QStringLiteral("dir"));
        if (!dir.isEmpty()) m_java->setManualJavaHome(dir);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // ---------- 第8项 WebUI 增强：代理绑定 / 看门狗 / 优化模组 / 异常纠错 ----------

    // 代理聚合列表（含每个代理绑定的服务器）
    if (api == QStringLiteral("/proxies") && method == QStringLiteral("GET")) {
        QJsonArray arr;
        if (m_proxyMgr) {
            const QVariantList list = m_proxyMgr->proxies();
            for (const QVariant &v : list) {
                QObject *p = v.value<QObject *>();
                if (!p) continue;
                QJsonObject o;
                o[QStringLiteral("instanceId")] = p->property("instanceId").toString();
                o[QStringLiteral("name")] = p->property("name").toString();
                o[QStringLiteral("type")] = p->property("type").toString();
                o[QStringLiteral("running")] = p->property("running").toBool();
                o[QStringLiteral("proxyPort")] = p->property("proxyPort").toInt();
                // 本代理绑定的服务器名列表
                QJsonArray bound;
                QVariantList boundList;
                QMetaObject::invokeMethod(const_cast<QObject *>(p), "serversBoundTo",
                    Qt::DirectConnection, Q_RETURN_ARG(QVariantList, boundList));
                for (const QVariant &sv : boundList) bound.append(sv.toString());
                o[QStringLiteral("servers")] = bound;
                arr.append(o);
            }
        }
        sendJson(sock, QJsonObject{{QStringLiteral("proxies"), arr}});
        return;
    }

    // 创建代理实例（POST body:{name}）：路由到真实 ProxyManager，由本地端执行
    if (api == QStringLiteral("/proxies") && method == QStringLiteral("POST")) {
        const QString name = bodyJson.value(QStringLiteral("name")).toString();
        const int idx = m_proxyMgr ? m_proxyMgr->addProxy(name) : -1;
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), idx >= 0},
                                   {QStringLiteral("index"), idx}});
        return;
    }
    // 代理实例操作（远控端/WebUI 控制真实代理实例；<id> 为 instanceId，空串=默认实例）
    if (api.startsWith(QStringLiteral("/proxies/"))) {
        const int slash = api.indexOf(QLatin1Char('/'), 9); // 9 = len("/proxies/") 前缀（index 8 为前缀斜杠）
        const QString id = (slash > 0) ? api.mid(9, slash - 9) : api.mid(9);
        const QString act = (slash > 0) ? api.mid(slash + 1) : QString();
        auto findProxy = [&](const QString &pid) -> QObject * {
            if (!m_proxyMgr) return nullptr;
            const QVariantList list = m_proxyMgr->proxies();
            for (const QVariant &v : list) {
                QObject *o = v.value<QObject *>();
                if (o && o->property("instanceId").toString() == pid)
                    return o;
            }
            return nullptr;
        };
        QObject *p = findProxy(id);
        if (!p) { sendStatus(sock, 404, QStringLiteral("proxy not found")); return; }
        if (act == QStringLiteral("install")) {
            QMetaObject::invokeMethod(p, "install", Qt::QueuedConnection);
        } else if (act == QStringLiteral("start")) {
            QMetaObject::invokeMethod(p, "start", Qt::QueuedConnection,
                                      Q_ARG(QString, bodyJson.value(QStringLiteral("javaPath")).toString()));
        } else if (act == QStringLiteral("stop")) {
            QMetaObject::invokeMethod(p, "stop", Qt::QueuedConnection);
        } else if (act == QStringLiteral("sync")) {
            bool ok = false;
            QMetaObject::invokeMethod(p, "syncConfig", Qt::DirectConnection, Q_RETURN_ARG(bool, ok));
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), ok}}); return;
        } else if (act == QStringLiteral("setServer")) {
            QMetaObject::invokeMethod(p, "setServerEnabled", Qt::QueuedConnection,
                                      Q_ARG(QString, bodyJson.value(QStringLiteral("name")).toString()),
                                      Q_ARG(bool, bodyJson.value(QStringLiteral("on")).toBool()));
        } else if (act == QStringLiteral("startBackend")) {
            QMetaObject::invokeMethod(p, "startBackend", Qt::QueuedConnection,
                                      Q_ARG(QString, bodyJson.value(QStringLiteral("name")).toString()));
        } else if (act == QStringLiteral("stopBackend")) {
            QMetaObject::invokeMethod(p, "stopBackend", Qt::QueuedConnection,
                                      Q_ARG(QString, bodyJson.value(QStringLiteral("name")).toString()));
        } else if (act == QStringLiteral("set")) {
            // 写回设置：属性存在才调用对应 setter（proxyPort/motd/offlineMode/autoRestart/
            // stopBackendsWithProxy/name/serverFilter 均带 WRITE）
            auto setProp = [&](const char *pname, const QVariant &val) {
                if (p->metaObject()->indexOfProperty(pname) >= 0)
                    p->setProperty(pname, val);
            };
            if (bodyJson.contains(QStringLiteral("proxyPort")))
                setProp("proxyPort", bodyJson.value(QStringLiteral("proxyPort")).toInt());
            if (bodyJson.contains(QStringLiteral("motd")))
                setProp("motd", bodyJson.value(QStringLiteral("motd")).toString());
            if (bodyJson.contains(QStringLiteral("offlineMode")))
                setProp("offlineMode", bodyJson.value(QStringLiteral("offlineMode")).toBool());
            if (bodyJson.contains(QStringLiteral("autoRestart")))
                setProp("autoRestart", bodyJson.value(QStringLiteral("autoRestart")).toBool());
            if (bodyJson.contains(QStringLiteral("stopBackendsWithProxy")))
                setProp("stopBackendsWithProxy", bodyJson.value(QStringLiteral("stopBackendsWithProxy")).toBool());
            if (bodyJson.contains(QStringLiteral("name")))
                setProp("name", bodyJson.value(QStringLiteral("name")).toString());
            if (bodyJson.contains(QStringLiteral("serverFilter"))) {
                QStringList fl;
                for (const QJsonValue &x : bodyJson.value(QStringLiteral("serverFilter")).toArray())
                    fl << x.toString();
                setProp("serverFilter", fl);
            }
        } else if (act == QStringLiteral("delete")) {
            const int index = m_proxyMgr->proxies().indexOf(QVariant::fromValue(p));
            const bool ok = (index >= 0) ? m_proxyMgr->removeProxy(index) : false;
            sendJson(sock, QJsonObject{{QStringLiteral("ok"), ok}}); return;
        } else {
            // 读操作（detail/console/backendSummary/serversBoundTo 共用此分支）：返回完整详情
            QJsonObject o;
            o[QStringLiteral("instanceId")] = p->property("instanceId").toString();
            o[QStringLiteral("name")] = p->property("name").toString();
            o[QStringLiteral("type")] = p->property("type").toString();
            o[QStringLiteral("running")] = p->property("running").toBool();
            o[QStringLiteral("installed")] = p->property("installed").toBool();
            o[QStringLiteral("busy")] = p->property("busy").toBool();
            o[QStringLiteral("status")] = p->property("status").toString();
            o[QStringLiteral("proxyPort")] = p->property("proxyPort").toInt();
            o[QStringLiteral("motd")] = p->property("motd").toString();
            o[QStringLiteral("installProgress")] = p->property("installProgress").toDouble();
            o[QStringLiteral("offlineMode")] = p->property("offlineMode").toBool();
            o[QStringLiteral("autoRestart")] = p->property("autoRestart").toBool();
            o[QStringLiteral("stopBackendsWithProxy")] = p->property("stopBackendsWithProxy").toBool();
            o[QStringLiteral("playerCount")] = p->property("playerCount").toInt();
            o[QStringLiteral("serverFilter")] = QJsonArray::fromStringList(p->property("serverFilter").toStringList());
            QString c; QMetaObject::invokeMethod(p, "getConsole", Qt::DirectConnection, Q_RETURN_ARG(QString, c));
            o[QStringLiteral("console")] = c;
            QVariantList bv; QMetaObject::invokeMethod(p, "backendSummary", Qt::DirectConnection, Q_RETURN_ARG(QVariantList, bv));
            o[QStringLiteral("backends")] = QJsonArray::fromVariantList(bv);
            QVariantList sv; QMetaObject::invokeMethod(p, "serversBoundTo", Qt::DirectConnection, Q_RETURN_ARG(QVariantList, sv));
            QJsonArray sarr; for (const QVariant &x : sv) sarr.append(x.toString());
            o[QStringLiteral("servers")] = sarr;
            sendJson(sock, o); return;
        }
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 服务器 → 代理绑定（POST body:{proxyId}；空串表示解绑）
    if (api.startsWith(QStringLiteral("/servers/")) && api.endsWith(QStringLiteral("/proxy")) && method == QStringLiteral("POST")) {
        const QString name = api.mid(9, api.length() - 9 - 6); // 去掉 "/servers/" 前缀与 "/proxy" 后缀
        const QString proxyId = bodyJson.value(QStringLiteral("proxyId")).toString();
        const bool ok = m_sm->setServerProxy(name, proxyId);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), ok}});
        return;
    }

    // 服务器看门狗状态
    if (api.startsWith(QStringLiteral("/servers/")) && api.endsWith(QStringLiteral("/watchdog")) && method == QStringLiteral("GET")) {
        const QString name = api.mid(9, api.length() - 9 - 10);
        Server *srv = m_sm->serverByName(name);
        if (!srv) { sendStatus(sock, 404, QStringLiteral("server not found")); return; }
        const QVariantMap wd = m_sc->watchdogStatus(srv->path());
        QJsonObject o = QJsonObject::fromVariantMap(wd);
        sendJson(sock, o);
        return;
    }

    // 异常纠错记录（活动异常列表）
    if (api == QStringLiteral("/errors") && method == QStringLiteral("GET")) {
        sendJson(sock, QJsonObject{{QStringLiteral("errors"), QJsonArray::fromVariantList(m_sc->errorRecords())}});
        return;
    }
    // 异常纠错手动操作：/errors/{path}/retry | /stop | /clear
    if (api.startsWith(QStringLiteral("/errors/")) && (api.endsWith(QStringLiteral("/retry"))
            || api.endsWith(QStringLiteral("/stop")) || api.endsWith(QStringLiteral("/clear")))) {
        QString rest = api.mid(7); // 去 "/errors/"
        const int slash = rest.lastIndexOf(QLatin1Char('/'));
        const QString path = HttpClient::urlDecode(rest.left(slash));
        const QString action = rest.mid(slash + 1);
        if (action == QStringLiteral("retry"))
            QMetaObject::invokeMethod(m_sc, "retryNow", Qt::QueuedConnection, Q_ARG(QString, path));
        else if (action == QStringLiteral("stop"))
            QMetaObject::invokeMethod(m_sc, "stopRetries", Qt::QueuedConnection, Q_ARG(QString, path));
        else
            QMetaObject::invokeMethod(m_sc, "clearError", Qt::QueuedConnection, Q_ARG(QString, path));
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 优化模组检索（触发 + 返回缓存）
    if (api.startsWith(QStringLiteral("/servers/")) && api.endsWith(QStringLiteral("/optimods")) && method == QStringLiteral("GET")) {
        const QString name = api.mid(9, api.length() - 9 - 9);
        Server *srv = m_sm->serverByName(name);
        if (!srv) { sendStatus(sock, 404, QStringLiteral("server not found")); return; }
        const QString mc = q.value(QStringLiteral("mc"));
        const QString loader = q.value(QStringLiteral("loader"));
        if (m_dc && !mc.isEmpty() && !loader.isEmpty())
            QMetaObject::invokeMethod(m_dc, "fetchOptimizationMods", Qt::QueuedConnection,
                Q_ARG(QString, mc), Q_ARG(QString, loader));
        sendJson(sock, QJsonObject{{QStringLiteral("mods"), QJsonArray::fromVariantList(m_optModsCache)},
                                   {QStringLiteral("loading"), mc.isEmpty() || loader.isEmpty() ? false : true}});
        return;
    }
    // 优化模组安装（POST body:{mc,loader}）
    if (api.startsWith(QStringLiteral("/servers/")) && api.endsWith(QStringLiteral("/installoptimod")) && method == QStringLiteral("POST")) {
        const QString name = api.mid(9, api.length() - 9 - 15);
        Server *srv = m_sm->serverByName(name);
        if (!srv) { sendStatus(sock, 404, QStringLiteral("server not found")); return; }
        const QString mc = bodyJson.value(QStringLiteral("mc")).toString();
        const QString loader = bodyJson.value(QStringLiteral("loader")).toString();
        if (m_dc)
            QMetaObject::invokeMethod(m_dc, "installOptimizationMods", Qt::QueuedConnection,
                Q_ARG(QString, srv->path()), Q_ARG(QString, mc), Q_ARG(QString, loader));
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 优化模组检索（catalog 级别，无需服务器名）：触发检索并轮询缓存结果
    if (api == QStringLiteral("/optimods") && method == QStringLiteral("GET")) {
        const QString mc = q.value(QStringLiteral("mc"));
        const QString loader = q.value(QStringLiteral("loader"));
        if (m_dc && !mc.isEmpty() && !loader.isEmpty())
            QMetaObject::invokeMethod(m_dc, "fetchOptimizationMods", Qt::QueuedConnection,
                Q_ARG(QString, mc), Q_ARG(QString, loader));
        sendJson(sock, QJsonObject{{QStringLiteral("mods"), QJsonArray::fromVariantList(m_optModsCache)},
                                   {QStringLiteral("loading"), mc.isEmpty() || loader.isEmpty() ? false : true}});
        return;
    }

    // ---------- 运维控制器（备份 / 定时任务 / 核心更新 / UPnP 端口映射）----------

    // 备份设置 + 状态
    if (api == QStringLiteral("/backup") && method == QStringLiteral("GET")) {
        QJsonObject o;
        if (m_backup) {
            o[QStringLiteral("enabled")] = m_backup->enabled();
            o[QStringLiteral("intervalHours")] = m_backup->intervalHours();
            o[QStringLiteral("retain")] = m_backup->retain();
            o[QStringLiteral("onStart")] = m_backup->onStart();
            o[QStringLiteral("lastResult")] = m_backup->lastResult();
            o[QStringLiteral("lastError")] = m_backup->lastError();
            o[QStringLiteral("lastBackup")] = qint64(m_backup->lastBackup());
        }
        o[QStringLiteral("servers")] = QJsonArray::fromVariantList(m_sm ? m_sm->serverSummary() : QVariantList());
        sendJson(sock, o);
        return;
    }
    if (api == QStringLiteral("/backup") && method == QStringLiteral("PUT")) {
        if (m_backup) {
            if (bodyJson.contains(QStringLiteral("enabled"))) m_backup->setEnabled(bodyJson.value(QStringLiteral("enabled")).toBool());
            if (bodyJson.contains(QStringLiteral("intervalHours"))) m_backup->setIntervalHours(bodyJson.value(QStringLiteral("intervalHours")).toInt());
            if (bodyJson.contains(QStringLiteral("retain"))) m_backup->setRetain(bodyJson.value(QStringLiteral("retain")).toInt());
            if (bodyJson.contains(QStringLiteral("onStart"))) m_backup->setOnStart(bodyJson.value(QStringLiteral("onStart")).toBool());
        }
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/backup/now") && method == QStringLiteral("POST")) {
        if (m_backup)
            m_backup->backupNow(bodyJson.value(QStringLiteral("name")).toString(),
                                bodyJson.value(QStringLiteral("path")).toString());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 定时任务
    if (api == QStringLiteral("/scheduler") && method == QStringLiteral("GET")) {
        sendJson(sock, QJsonObject{{QStringLiteral("tasks"), QJsonArray::fromVariantList(m_sched ? m_sched->tasks() : QVariantList())}});
        return;
    }
    if (api == QStringLiteral("/scheduler") && method == QStringLiteral("POST")) {
        int idx = -1;
        if (m_sched) idx = m_sched->addTask(bodyJson.value(QStringLiteral("task")).toVariant().toMap());
        sendJson(sock, QJsonObject{{QStringLiteral("index"), idx}, {QStringLiteral("ok"), idx >= 0}});
        return;
    }
    if (api == QStringLiteral("/scheduler") && method == QStringLiteral("PUT")) {
        if (m_sched) m_sched->updateTask(bodyJson.value(QStringLiteral("index")).toInt(),
                                          bodyJson.value(QStringLiteral("task")).toVariant().toMap());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/scheduler") && method == QStringLiteral("DELETE")) {
        int idx = bodyJson.value(QStringLiteral("index")).toInt(-1);
        if (idx < 0) { bool ok = false; int v = q.value(QStringLiteral("index")).toInt(&ok); if (ok) idx = v; }
        if (m_sched && idx >= 0) m_sched->removeTask(idx);
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), idx >= 0}});
        return;
    }
    if (api == QStringLiteral("/scheduler/run") && method == QStringLiteral("POST")) {
        if (m_sched) m_sched->runTask(bodyJson.value(QStringLiteral("index")).toInt());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 核心更新（异步；结果经信号，远控端以 toast/状态轮询感知；此处先触发）
    if (api == QStringLiteral("/update/check") && method == QStringLiteral("POST")) {
        if (m_update) m_update->checkUpdate(bodyJson.value(QStringLiteral("name")).toString(),
                                             bodyJson.value(QStringLiteral("type")).toString(),
                                             bodyJson.value(QStringLiteral("version")).toString());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/update/do") && method == QStringLiteral("POST")) {
        if (m_update) m_update->updateJar(bodyJson.value(QStringLiteral("name")).toString(),
                                          bodyJson.value(QStringLiteral("path")).toString(),
                                          bodyJson.value(QStringLiteral("type")).toString(),
                                          bodyJson.value(QStringLiteral("version")).toString());
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/update") && method == QStringLiteral("GET")) {
        sendJson(sock, QJsonObject{{QStringLiteral("info"), QJsonObject::fromVariantMap(m_updateInfo)}});
        return;
    }

    // UPnP 端口映射
    if (api == QStringLiteral("/portmap") && method == QStringLiteral("GET")) {
        QJsonObject o;
        if (m_pmap) {
            o[QStringLiteral("available")] = m_pmap->available();
            o[QStringLiteral("busy")] = m_pmap->busy();
            o[QStringLiteral("status")] = m_pmap->status();
            o[QStringLiteral("externalIp")] = m_pmap->externalIp();
            o[QStringLiteral("gatewayName")] = m_pmap->gatewayName();
            o[QStringLiteral("lanIp")] = m_pmap->lanIp();
            o[QStringLiteral("mappings")] = QJsonArray::fromVariantList(m_pmap->mappings());
        }
        sendJson(sock, o);
        return;
    }
    if (api == QStringLiteral("/portmap/discover") && method == QStringLiteral("GET")) {
        if (m_port) m_pmap->discover();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/portmap/add") && method == QStringLiteral("GET")) {
        if (m_port) m_pmap->addMapping(q.value(QStringLiteral("ext")).toInt(),
                                       q.value(QStringLiteral("int")).toInt(),
                                       q.value(QStringLiteral("proto")),
                                       q.value(QStringLiteral("desc")));
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/portmap/remove") && method == QStringLiteral("GET")) {
        if (m_port) m_pmap->removeMapping(q.value(QStringLiteral("ext")).toInt(),
                                          q.value(QStringLiteral("proto")));
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }
    if (api == QStringLiteral("/portmap/external") && method == QStringLiteral("GET")) {
        if (m_port) m_pmap->refreshExternalIp();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // WebUI 自身设置（端口/令牌/暴露范围），由 SettingsController 持久化
    if (api == QStringLiteral("/webui") && method == QStringLiteral("GET")) {
        QJsonObject o;
        if (m_settings) {
            o[QStringLiteral("enabled")] = m_settings->webuiEnabled();
            o[QStringLiteral("port")] = m_settings->webuiPort();
            o[QStringLiteral("token")] = m_settings->webuiToken();
            o[QStringLiteral("exposeLan")] = m_settings->webuiExposeLan();
            o[QStringLiteral("certPath")] = m_settings->webuiCertPath();
            o[QStringLiteral("keyPath")] = m_settings->webuiKeyPath();
        }
        sendJson(sock, o);
        return;
    }
    if (api == QStringLiteral("/webui") && method == QStringLiteral("PUT")) {
        if (m_settings) {
            if (bodyJson.contains(QStringLiteral("port"))) m_settings->setWebuiPort(bodyJson.value(QStringLiteral("port")).toInt());
            if (bodyJson.contains(QStringLiteral("exposeLan"))) m_settings->setWebuiExposeLan(bodyJson.value(QStringLiteral("exposeLan")).toBool());
            if (bodyJson.contains(QStringLiteral("certPath"))) m_settings->setWebuiCertPath(bodyJson.value(QStringLiteral("certPath")).toString());
            if (bodyJson.contains(QStringLiteral("keyPath"))) m_settings->setWebuiKeyPath(bodyJson.value(QStringLiteral("keyPath")).toString());
            if (bodyJson.contains(QStringLiteral("token"))) m_settings->setWebuiToken(bodyJson.value(QStringLiteral("token")).toString());
            if (bodyJson.contains(QStringLiteral("enabled"))) m_settings->setWebuiEnabled(bodyJson.value(QStringLiteral("enabled")).toBool());
            m_settings->apply();
        }
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 远控端 WebUI 状态聚合（enabled/running/paired/连接状态/port/https/error）
    if (api == QStringLiteral("/webui/status") && method == QStringLiteral("GET")) {
        QJsonObject o;
        o[QStringLiteral("enabled")] = m_enabled;
        o[QStringLiteral("running")] = isRunning();
        o[QStringLiteral("paired")] = m_pairUsed;
        o[QStringLiteral("mobileConnected")] = m_mobileConnected;
        o[QStringLiteral("remoteConnected")] = m_remoteConnected;
        o[QStringLiteral("remoteClients")] = QJsonObject::fromVariantMap(m_remoteClients);
        o[QStringLiteral("port")] = m_port;
        o[QStringLiteral("https")] = m_https;
        o[QStringLiteral("error")] = m_error;
        sendJson(sock, o);
        return;
    }
    // 远控端：触发本地端生成新配对码（一次性），返回码/URI/二维码/有效期
    if (api == QStringLiteral("/webui/pair/generate") && method == QStringLiteral("POST")) {
        generatePairCode();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int remainSec = int((10 * 60 * 1000 - (now - m_pairGenMs)) / 1000);
        sendJson(sock, QJsonObject{
            {QStringLiteral("code"), m_pairCode},
            {QStringLiteral("uri"), pairUri()},
            {QStringLiteral("qr"), generateQrSvg(pairUri())},
            {QStringLiteral("remainSec"), remainSec > 0 ? remainSec : 0},
            {QStringLiteral("used"), m_pairUsed}
        });
        return;
    }
    // 远控端：重新监听 WebUI（端口/暴露范围/证书变化时）
    if (api == QStringLiteral("/webui/rebind") && method == QStringLiteral("POST")) {
        rebind();
        sendJson(sock, QJsonObject{{QStringLiteral("ok"), true}});
        return;
    }

    // 整体状态（用于初始化：主题/设置）
    if (api == QStringLiteral("/state") && method == QStringLiteral("GET")) {
        sendJson(sock, statePayload());
        return;
    }
    // 轻量系统监控：仅 CPU/内存。侧栏监控改用它，避免每 3 秒触发 serverListWithRunning()
    if (api == QStringLiteral("/system") && method == QStringLiteral("GET")) {
        sendJson(sock, systemPayload());
        return;
    }

    // 远控端：判断被控端某路径是否存在（创建/导入目录占用校验用）
    if (api == QStringLiteral("/fs/exists") && method == QStringLiteral("GET")) {
        const QString p = q.value(QStringLiteral("path"));
        sendJson(sock, QJsonObject{{QStringLiteral("exists"), !p.isEmpty() && QFileInfo::exists(p)}});
        return;
    }
    // 远控端：被控端探测某端口是否空闲（命中即关闭，不真正监听）
    if (api == QStringLiteral("/portfree") && method == QStringLiteral("GET")) {
        const int port = q.value(QStringLiteral("port")).toInt();
        bool free = false;
        if (port > 0 && port < 65536) {
            QTcpServer probe;
            free = probe.listen(QHostAddress::AnyIPv4, quint16(port));
            probe.close();
        }
        sendJson(sock, QJsonObject{{QStringLiteral("free"), free}});
        return;
    }
    // 远控端：在被控端从 from 起扫描首个空闲端口（assignFreePort 用）
    if (api == QStringLiteral("/assignfreeport") && method == QStringLiteral("GET")) {
        const int from = q.value(QStringLiteral("from")).toInt();
        const int to = q.value(QStringLiteral("to")).toInt();
        int found = -1;
        for (int p = from; p <= to && p < 65536; ++p) {
            QTcpServer probe;
            if (probe.listen(QHostAddress::AnyIPv4, quint16(p))) { found = p; probe.close(); break; }
            probe.close();
        }
        sendJson(sock, QJsonObject{{QStringLiteral("port"), found}});
        return;
    }

    sendStatus(sock, 404, QStringLiteral("Not Found"));
}

#include "webuiserver_spa.inc"
