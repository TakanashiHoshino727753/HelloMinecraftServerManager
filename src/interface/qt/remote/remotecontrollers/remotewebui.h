#pragma once
#include <QObject>
#include <QVariantMap>
#include <QTimer>
class RemoteClient;

// 远端 WebUI 服务控制（镜像 desktop WebuiServer 对 QML 暴露的接口）。
// 远控端本身不运行 WebUI 服务（服务在本地端），这里转发到本地端 WebUI 状态/配对端点，
// 使照搬来的 PairControl.qml / ControllerSettings.qml 无需改动即可绑定（无服务端时全部为 false，UI 优雅降级）。
class RemoteWebuiServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(bool mobileConnected READ mobileConnected NOTIFY changed)
    Q_PROPERTY(bool remoteConnected READ remoteConnected NOTIFY changed)
    Q_PROPERTY(QVariantMap remoteClients READ remoteClients NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(bool https READ https NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit RemoteWebuiServer(RemoteClient *rc, QObject *parent = nullptr);
    bool enabled() const { return m_data.value(QStringLiteral("enabled")).toBool(); }
    bool running() const { return m_data.value(QStringLiteral("running")).toBool(); }
    bool paired() const { return m_data.value(QStringLiteral("paired")).toBool(); }
    bool mobileConnected() const { return m_data.value(QStringLiteral("mobileConnected")).toBool(); }
    bool remoteConnected() const { return m_data.value(QStringLiteral("remoteConnected")).toBool(); }
    QVariantMap remoteClients() const { return m_data.value(QStringLiteral("remoteClients")).toMap(); }
    int port() const { return m_data.value(QStringLiteral("port")).toInt(); }
    bool https() const { return m_data.value(QStringLiteral("https")).toBool(); }
    QString error() const { return m_data.value(QStringLiteral("error")).toString(); }
    Q_INVOKABLE QString pairUri() const;
    Q_INVOKABLE QString generatePairCode();
    Q_INVOKABLE void rebind();
    Q_INVOKABLE void refresh();
signals:
    void changed();
private:
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};
