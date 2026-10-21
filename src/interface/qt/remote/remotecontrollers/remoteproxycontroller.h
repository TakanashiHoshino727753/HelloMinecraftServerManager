// remoteproxycontroller.h —— 远控端对桌面端 ProxyController 的镜像
// 桌面端 ProxyController 直接管理 velocity.jar 下载/进程；远控端改为轮询本地端 WebUI：
//   GET  /api/proxies/<id>          完整详情（running/status/installProgress/console/backends/servers…）
//   POST /api/proxies/<id>/install|start|stop|sync|setServer|startBackend|stopBackend|set|delete
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

class RemoteClient;

class RemoteProxyController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY proxyPortChanged)
    Q_PROPERTY(QString motd READ motd WRITE setMotd NOTIFY motdChanged)
    Q_PROPERTY(qreal installProgress READ installProgress NOTIFY installProgressChanged)
    Q_PROPERTY(QString proxyDir READ proxyDir CONSTANT)
    Q_PROPERTY(bool offlineMode READ offlineMode WRITE setOfflineMode NOTIFY offlineModeChanged)
    Q_PROPERTY(bool stopBackendsWithProxy READ stopBackendsWithProxy WRITE setStopBackendsWithProxy NOTIFY stopBackendsWithProxyChanged)
    Q_PROPERTY(bool autoRestart READ autoRestart WRITE setAutoRestart NOTIFY autoRestartChanged)
    Q_PROPERTY(QString instanceId READ instanceId CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QStringList serverFilter READ serverFilter WRITE setServerFilter NOTIFY serverFilterChanged)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY playerCountChanged)
    Q_PROPERTY(QString type READ type CONSTANT)
    // 镜像 desktop ProxyController/Proxy 的别名（QML 中 proxy.port / proxy.players 与 proxy.proxyPort / proxy.playerCount 并存）
    Q_PROPERTY(int port READ port NOTIFY proxyPortChanged)
    Q_PROPERTY(int players READ players NOTIFY playerCountChanged)
public:
    explicit RemoteProxyController(RemoteClient *rc, const QString &instanceId,
                                   const QString &name = QString(), QObject *parent = nullptr);
    bool running() const { return m_running; }
    bool installed() const { return m_installed; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int proxyPort() const { return m_proxyPort; }
    QString motd() const { return m_motd; }
    qreal installProgress() const { return m_installProgress; }
    QString proxyDir() const;
    bool offlineMode() const { return m_offlineMode; }
    bool stopBackendsWithProxy() const { return m_stopBackendsWithProxy; }
    bool autoRestart() const { return m_autoRestart; }
    QString instanceId() const { return m_instanceId; }
    QString name() const { return m_name; }
    QStringList serverFilter() const { return m_serverFilter; }
    int playerCount() const { return m_playerCount; }
    QString type() const { return m_type; }
    int port() const { return m_proxyPort; }
    int players() const { return m_playerCount; }

    Q_INVOKABLE void setProxyPort(int p);
    Q_INVOKABLE void setMotd(const QString &m);
    Q_INVOKABLE void setOfflineMode(bool v);
    Q_INVOKABLE void setStopBackendsWithProxy(bool v);
    Q_INVOKABLE void setAutoRestart(bool v);
    Q_INVOKABLE void setName(const QString &n);
    Q_INVOKABLE void setServerFilter(const QStringList &f);

    Q_INVOKABLE void setServerEnabled(const QString &serverName, bool on);
    Q_INVOKABLE QVariantList serversBoundTo() const { return m_serversBound; }
    Q_INVOKABLE void startBackend(const QString &serverName);
    Q_INVOKABLE void stopBackend(const QString &serverName);

    Q_INVOKABLE void install();
    Q_INVOKABLE bool syncConfig();
    Q_INVOKABLE void start(const QString &javaPath = QString());
    Q_INVOKABLE void stop();
    Q_INVOKABLE QVariantList backendSummary() const { return m_backendSummary; }
    Q_INVOKABLE QString getConsole() const { return m_console; }

    void refresh();   // 拉取最新详情并更新缓存（由 RemoteProxyManager 周期性调用）

signals:
    void runningChanged();
    void installedChanged();
    void busyChanged();
    void statusChanged();
    void proxyPortChanged();
    void motdChanged();
    void installProgressChanged();
    void offlineModeChanged();
    void stopBackendsWithProxyChanged();
    void autoRestartChanged();
    void nameChanged();
    void serverFilterChanged();
    void playerCountChanged();
    void consoleAppended(const QString &line);

private:
    void applyDetail(const QJsonObject &o);
    void postSet(const QJsonObject &patch);
    RemoteClient *m_rc;
    QString m_instanceId;
    QString m_name;
    bool m_running = false;
    bool m_installed = false;
    bool m_busy = false;
    QString m_status;
    int m_proxyPort = 25577;
    QString m_motd;
    qreal m_installProgress = 0;
    bool m_offlineMode = true;
    bool m_stopBackendsWithProxy = true;
    bool m_autoRestart = true;
    int m_playerCount = 0;
    QString m_type;
    QStringList m_serverFilter;
    QString m_console;
    QVariantList m_backendSummary;
    QVariantList m_serversBound;
};
