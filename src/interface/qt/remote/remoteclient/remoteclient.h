// remoteclient.h —— HMCSM_Remote 远控端核心：连接本地端控制器并转发操作
// 本地端通过 WebUIServer (默认 25575) 暴露 REST API（无 WebSocket，全轮询）。
// 鉴权：Authorization: Bearer <token> 或 ?token=<token>；HTTPS 自签需忽略证书错误。
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QTimer>

class RemoteClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString host READ host NOTIFY hostChanged)
    Q_PROPERTY(int port READ port NOTIFY portChanged)
    Q_PROPERTY(QString token READ token NOTIFY tokenChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QJsonArray servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(bool https READ https NOTIFY httpsChanged)
    // 配对/连接状态（供配对页与设置页展示）
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY pairCodeChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(bool paired READ paired NOTIFY pairedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceIdChanged)

public:
    explicit RemoteClient(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString host() const { return m_host; }
    int port() const { return m_port; }
    QString token() const { return m_token; }
    QString lastError() const { return m_lastError; }
    QJsonArray servers() const { return m_servers; }
    bool https() const { return m_https; }

    Q_INVOKABLE void connectTo(const QString &host, int port, const QString &token, bool https = true);
    Q_INVOKABLE void connectFromUri(const QString &hmcsmUri);   // 解析 hmcsm://token@host:port?t=...
    Q_INVOKABLE void disconnectFrom();
    Q_INVOKABLE bool parseUri(const QString &hmcsmUri, QVariantMap &out);   // 仅供 QML 预览

    // —— 设备配对（独立端口，换取长期稳定的 deviceToken，与 webuiToken 解耦）——
    // 粘贴本地端二维码 URI（hmcsm-remote://CODE@host:port）即可配对
    Q_INVOKABLE void pairWithUri(const QString &remoteUri, const QString &name);
    // 手动：host/port + 本地端显示的配对码
    Q_INVOKABLE void startPair(const QString &host, int port, bool https);
    Q_INVOKABLE void finishPair(const QString &code, const QString &host, int port, bool https, const QString &name);
    Q_INVOKABLE void forgetPair();          // 清除本地存储的配对，断开
    Q_INVOKABLE void tryAutoConnect();      // 启动时若已配对则自动重连（抗令牌轮换）

    // —— 服务器操作（直接转发本地端 HTTP 接口）——
    Q_INVOKABLE void refreshServers();
    Q_INVOKABLE void startServer(const QString &name, int min = 1024, int max = 2048);
    Q_INVOKABLE void stopServer(const QString &name);
    Q_INVOKABLE void forceStopServer(const QString &name);
    Q_INVOKABLE void sendCommand(const QString &name, const QString &cmd);

    // —— 一键安装优化模组 ——
    // 触发检索（mc 如 "1.20.1"，loader: fabric/forge/neoforge），随后自动轮询直至返回 mods
    Q_INVOKABLE void fetchOptimods(const QString &name, const QString &mc, const QString &loader);
    Q_INVOKABLE void installOptimod(const QString &name, const QString &mc, const QString &loader);
    Q_PROPERTY(QJsonArray optMods READ optMods NOTIFY optModsChanged)
    Q_PROPERTY(bool optLoading READ optLoading NOTIFY optLoadingChanged)
    QJsonArray optMods() const { return m_optMods; }
    bool optLoading() const { return m_optLoading; }

    Q_INVOKABLE QString toHmcsmUri() const;   // 生成 hmcsm:// 供分享（本地端已有，但远控端也可展示）

    // —— REST 底层（供远控端各控制器 shim 复用，转发到本地端 WebUI）——
    QNetworkReply *apiGet(const QString &path, const QVariantMap &query = {});
    QNetworkReply *apiPost(const QString &path, const QJsonObject &body = {});
    QNetworkReply *apiPut(const QString &path, const QJsonObject &body = {});
    QNetworkReply *apiDelete(const QString &path);

signals:
    void connectedChanged();
    void hostChanged();
    void portChanged();
    void tokenChanged();
    void lastErrorChanged();
    void serversChanged();
    void httpsChanged();
    void pairCodeChanged();
    void pairingChanged();
    void pairedChanged();
    void deviceNameChanged();
    void deviceIdChanged();
    void optModsChanged();
    void optLoadingChanged();
    void toast(const QString &title, const QString &text);   // 给 QML 弹提示

private:
    void setConnected(bool v);
    void setError(const QString &e);
    void applyToken(QNetworkRequest &req);
    void applySsl(QNetworkRequest &req);   // HTTPS 时忽略自签证书
    QUrl buildUrl(const QString &path, const QVariantMap &query) const;
    void loadStored();
    void saveStored();
    QString pairCode() const { return m_pairCode; }
    bool pairing() const { return m_pairing; }
    bool paired() const { return !m_deviceToken.isEmpty(); }
    QString deviceName() const { return m_deviceName; }
    QString deviceId() const { return m_deviceId; }

    QNetworkAccessManager *m_nam = nullptr;
    QString m_host;
    int m_port = 25575;
    QString m_token;
    bool m_https = true;
    bool m_connected = false;
    QString m_lastError;
    QJsonArray m_servers;
    QJsonArray m_optMods;
    bool m_optLoading = false;
    QTimer *m_pollTimer = nullptr;        // 服务器列表轮询（2s）
    QTimer *m_optPollTimer = nullptr;     // 优化模组检索轮询（1.5s）
    QString m_optName;                    // 正在检索的服务器名

    // 配对状态
    QString m_pairCode;
    bool m_pairing = false;
    QString m_deviceToken;                // 长期稳定凭证（持久化）
    QString m_deviceName;
    QString m_deviceId;
    QString m_pendingHost;                // startPair 暂存的待配对地址
    int m_pendingPort = 25577;
    bool m_pendingHttps = false;
};
