// remoteserver.h —— 远控端对桌面端 ServerManager / ServerController 的 REST 转发镜像
// 数据全部来自本地端 WebUI（/api/servers 列表 + /api/servers/:name 详情），经 RemoteClient 转发。
// QML 接口与桌面端严格一致（serverManager / serverController），仅实现改为网络请求。
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QQmlListProperty>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QHash>
#include <QTimer>

class RemoteClient;

// 单个服务器数据行（与桌面端 Server 同构，供 QQmlListProperty 作为 model）
class RemoteServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY versionChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString proxyId READ proxyId WRITE setProxyId NOTIFY proxyIdChanged)
public:
    explicit RemoteServer(QObject *parent = nullptr) : QObject(parent) {}
    RemoteServer(const QString &name, const QString &version, const QString &type,
                 const QString &path, const QString &id = QString(),
                 const QString &proxyId = QString(), QObject *parent = nullptr)
        : QObject(parent), m_name(name), m_version(version), m_type(type),
          m_path(path), m_id(id), m_proxyId(proxyId) {}
    QString name() const { return m_name; }
    void setName(const QString &v) { if (m_name != v) { m_name = v; emit nameChanged(); } }
    QString version() const { return m_version; }
    void setVersion(const QString &v) { if (m_version != v) { m_version = v; emit versionChanged(); } }
    QString type() const { return m_type; }
    void setType(const QString &v) { if (m_type != v) { m_type = v; emit typeChanged(); } }
    QString path() const { return m_path; }
    void setPath(const QString &v) { if (m_path != v) { m_path = v; emit pathChanged(); } }
    QString id() const { return m_id; }
    void setId(const QString &v) { if (m_id != v) { m_id = v; emit idChanged(); } }
    QString proxyId() const { return m_proxyId; }
    void setProxyId(const QString &v) { if (m_proxyId != v) { m_proxyId = v; emit proxyIdChanged(); } }
signals:
    void nameChanged();
    void versionChanged();
    void typeChanged();
    void pathChanged();
    void idChanged();
    void proxyIdChanged();
private:
    QString m_name, m_version, m_type, m_path, m_id, m_proxyId;
};

// 服务器集合（镜像 ServerManager）：从 /api/servers 轮询构建 Server 列表
class RemoteServerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<RemoteServer> servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(int count READ count NOTIFY serversChanged)
public:
    explicit RemoteServerManager(RemoteClient *rc, QObject *parent = nullptr);
    QQmlListProperty<RemoteServer> servers();
    int count() const { return m_servers.size(); }

    Q_INVOKABLE QVariantList serverSummary() const;
    Q_INVOKABLE int indexOfServer(const QString &name) const;
    Q_INVOKABLE void scanServers();   // 触发一次刷新（镜像桌面端“刷新服务器”）
    Q_INVOKABLE void removeServer(int index);
    Q_INVOKABLE bool setServerProxy(const QString &name, const QString &proxyId);

signals:
    void serversChanged();

private:
    void rebuild(const QJsonArray &arr);
    static void appendServer(QQmlListProperty<RemoteServer> *prop, RemoteServer *s);
    static RemoteServer *serverAt(QQmlListProperty<RemoteServer> *prop, long long i);
    static long long serverCount(QQmlListProperty<RemoteServer> *prop);
    static void clearServers(QQmlListProperty<RemoteServer> *prop);

    RemoteClient *m_rc;
    QList<RemoteServer *> m_servers;
};

// 服务器控制（镜像 ServerController）：启动/停止/发指令/控制台/属性/mods/异常纠错
class RemoteServerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningCountChanged)
    Q_PROPERTY(QVariantList errorRecords READ errorRecords NOTIFY errorRecordsChanged)
    // 崩溃自动重启配置（镜像 desktop ServerController）
    Q_PROPERTY(bool autoRestart READ autoRestart WRITE setAutoRestart NOTIFY cfgChanged)
    Q_PROPERTY(int maxRetries READ maxRetries WRITE setMaxRetries NOTIFY cfgChanged)
    Q_PROPERTY(int backoffSec READ backoffSec WRITE setBackoffSec NOTIFY cfgChanged)
public:
    explicit RemoteServerController(RemoteClient *rc, QObject *parent = nullptr);
    int runningCount() const { return m_running.count(); }

    // 活动异常记录（远控端暂以本地端 /api/system 与运行态推断；完整纠错需 WebUI 端点，后续补）
    QVariantList errorRecords() const { return m_errorRecords; }

    bool autoRestart() const { return m_cfg.value(QStringLiteral("autoRestart")).toBool(); }
    int maxRetries() const { return m_cfg.value(QStringLiteral("maxRetries")).toInt(); }
    int backoffSec() const { return m_cfg.value(QStringLiteral("backoffSec")).toInt(); }
    void setAutoRestart(bool v) { m_cfg.insert(QStringLiteral("autoRestart"), v); emit cfgChanged(); }
    void setMaxRetries(int v) { m_cfg.insert(QStringLiteral("maxRetries"), v); emit cfgChanged(); }
    void setBackoffSec(int v) { m_cfg.insert(QStringLiteral("backoffSec"), v); emit cfgChanged(); }
    Q_INVOKABLE QVariantList runningServerNames() const;

    Q_INVOKABLE bool isRunning(const QString &path) const { return m_running.contains(path); }
    Q_INVOKABLE bool pathExists(const QString &path) const;
    Q_INVOKABLE void start(const QString &name, const QString &path,
                           const QString &javaPath = QString(), int minMem = 1024,
                           int maxMem = 2048, bool resetRetry = true);
    Q_INVOKABLE void stop(const QString &path);
    Q_INVOKABLE void forceStop(const QString &path);
    Q_INVOKABLE void send(const QString &path, const QString &cmd);
    Q_INVOKABLE QString getConsole(const QString &path) const;
    Q_INVOKABLE QStringList players(const QString &path) const;
    Q_INVOKABLE QStringList listMods(const QString &path) const;
    Q_INVOKABLE QVariantMap readProperties(const QString &path);
    Q_INVOKABLE void writeProperties(const QString &path, const QVariantMap &map);
    Q_INVOKABLE QVariantList runningServerUsages() const;
    Q_INVOKABLE void retryNow(const QString &path);
    Q_INVOKABLE void stopRetries(const QString &path);
    Q_INVOKABLE void clearError(const QString &path);
    Q_INVOKABLE int serverPort(const QString &path) const;
    Q_INVOKABLE bool isPortFree(int port) const;
    Q_INVOKABLE int assignFreePort(const QString &path);

signals:
    void consoleAppended(const QString &name, const QString &line);
    void stateChanged(const QString &name, bool running);
    void playersChanged(const QString &name, const QStringList &players);
    void runningCountChanged();
    void errorRecordsChanged();
    void cfgChanged();
    void portConflict(const QString &name, const QString &path, int port, const QString &holder);

private:
    void refreshList();                 // 轮询 /api/servers 刷新运行态 + 详情缓存
    void refreshDetails();              // 轮询每台服务器 /api/servers/:name 详情
    void applyServerJson(const QJsonObject &o);  // 合并一台服务器详情到缓存

    RemoteClient *m_rc;
    QSet<QString> m_running;                    // 运行中的 path 集合
    QHash<QString, QJsonObject> m_detail;       // path -> 最近一次详情
    QHash<QString, QString> m_nameByPath;       // path -> name
    QVariantList m_errorRecords;
    QVariantMap m_cfg;
    double m_sysCpu = 0, m_sysMem = 0;
    QTimer *m_listTimer = nullptr;
    QTimer *m_detailTimer = nullptr;
    QTimer *m_sysTimer = nullptr;
};
