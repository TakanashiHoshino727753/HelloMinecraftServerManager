#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QNetworkAccessManager>
#include <QTimer>

class SettingsController;

// 远控端控制服务（独立于 WebUI）：在独立端口监听，负责“设备配对”与“控制反代”。
//   - 配对：本地端设置页点“生成远控配对码”→ 生成一次性 6 位码 + hmcsm-remote:// 二维码 URI；
//     远控端扫码/输入码后调用 /remote/pair/finish 换取长期稳定的 deviceToken（与 webuiToken 解耦，
//     即便本地端重置访问令牌也不影响已配对设备，远控端可凭 deviceToken 直接重连）。
//   - 控制：已鉴权的 /api/* 请求被反代到本机 WebUIServer 回环地址（携带 webuiToken），
//     因此“远控功能”不依赖 WebUI 前台是否开启（WebUIServer 始终监听回环供反代）。
// 自定义 QTcpServer：直接把原生 socket 描述符交给上层（明文用 QTcpSocket、HTTPS 用 QSslSocket 接管），
// 避免在 nextPendingConnection 已经持有描述符后再转 QSslSocket 导致 fd 所有权冲突。
class RemoteTcpServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit RemoteTcpServer(QObject *parent = nullptr) : QTcpServer(parent) {}
signals:
    void newDescriptor(qintptr fd);
protected:
    void incomingConnection(qintptr socketDescriptor) override { emit newDescriptor(socketDescriptor); }
};

class RemoteControlServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY pairCodeChanged)
    Q_PROPERTY(QString pairUri READ pairUri NOTIFY pairUriChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY devicesChanged)
    Q_PROPERTY(int remotePort READ remotePort WRITE setRemotePort NOTIFY remotePortChanged)
    Q_PROPERTY(bool exposeLan READ exposeLan WRITE setExposeLan NOTIFY exposeLanChanged)
public:
    explicit RemoteControlServer(SettingsController *settings, QObject *parent = nullptr);

    QString pairCode() const { return m_pairCode; }
    QString pairUri() const { return m_pairUri; }
    bool pairing() const { return m_pairing; }
    QVariantList devices() const { return m_devices; }
    int deviceCount() const { return m_devices.size(); }
    int remotePort() const { return m_port; }
    bool exposeLan() const { return m_exposeLan; }
    bool https() const { return m_https; }
    void setRemotePort(int p);
    void setExposeLan(bool on);

    // 启动/停止监听（随桌面端启动自动 start()）
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // 本地端设置页：生成一个新的配对码与二维码 URI（旧码立即作废）
    Q_INVOKABLE void generatePairCode();
    // 撤销某个已配对设备（id）
    Q_INVOKABLE void revokeDevice(const QString &id);

signals:
    void pairCodeChanged();
    void pairUriChanged();
    void pairingChanged();
    void devicesChanged();
    void remotePortChanged();
    void exposeLanChanged();
    void activity(const QString &platform);   // 有远控端请求到达（用于连接状态展示）

private:
    void loadDevices();
    void saveDevices();
    QString storePath() const;
    QString localUriHost() const;
    void handleDescriptor(qintptr fd);
    void onReadyRead(QTcpSocket *s);
    void processRequest(QTcpSocket *s, const QByteArray &raw);
    void writeResponse(QTcpSocket *s, int code, const QByteArray &ct, const QByteArray &body);
    bool validDeviceToken(const QString &tok, QVariantMap &dev) const;
    void proxyToWebui(QTcpSocket *s, const QString &method, const QString &path,
                      const QByteArray &query, const QByteArray &body, const QByteArray &ct);
    // TLS：复用 WebUI 自签证书（hmcsm_webui.crt/key），提供与 WebUI 同等的 HTTPS
    void loadTls();
    QString certDir() const;

    SettingsController *m_settings;
    RemoteTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_pairTimer = nullptr;   // 配对码过期清理

    QString m_pairCode;
    QString m_pairUri;
    bool m_pairing = false;
    qint64 m_pairExpiresMs = 0;

    QVariantList m_devices;          // 已配对设备 {id,name,platform,token,createdMs,lastSeenMs}
    int m_port = 25577;
    bool m_exposeLan = false;
    bool m_https = false;            // 是否以 HTTPS 提供（依赖证书可用性）
    QSslConfiguration m_sslConf;      // 复用 WebUI 自签证书
    QString m_webuiToken;            // 缓存，随 settings.webuiTokenChanged 更新

    QMap<QTcpSocket *, QByteArray> m_buffers;
};
