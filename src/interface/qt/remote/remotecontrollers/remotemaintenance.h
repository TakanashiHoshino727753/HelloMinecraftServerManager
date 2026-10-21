#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
class RemoteClient;

// 远端端口映射（镜像 desktop PortMapper）
class RemotePortMapper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    // 镜像 desktop PortMapper 具名属性
    Q_PROPERTY(bool busy READ busy NOTIFY dataChanged)
    Q_PROPERTY(bool available READ available NOTIFY dataChanged)
    Q_PROPERTY(QString status READ status NOTIFY dataChanged)
    Q_PROPERTY(QString gatewayName READ gatewayName NOTIFY dataChanged)
    Q_PROPERTY(QString lanIp READ lanIp NOTIFY dataChanged)
    Q_PROPERTY(QString externalIp READ externalIp NOTIFY dataChanged)
    Q_PROPERTY(QVariantList mappings READ mappings NOTIFY dataChanged)
public:
    explicit RemotePortMapper(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }
    bool busy() const { return m_data.value(QStringLiteral("busy")).toBool(); }
    bool available() const { return m_data.value(QStringLiteral("available")).toBool(); }
    QString status() const { return m_data.value(QStringLiteral("status")).toString(); }
    QString gatewayName() const { return m_data.value(QStringLiteral("gatewayName")).toString(); }
    QString lanIp() const { return m_data.value(QStringLiteral("lanIp")).toString(); }
    QString externalIp() const { return m_data.value(QStringLiteral("externalIp")).toString(); }
    QVariantList mappings() const { return m_data.value(QStringLiteral("mappings")).toList(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void discover();
    Q_INVOKABLE void addMapping(int ext, int in, const QString &proto, const QString &desc);
    Q_INVOKABLE void removeMapping(int ext, const QString &proto);
    Q_INVOKABLE void refreshExternalIp();
signals:
    void dataChanged();
private:
    void apply(const QJsonObject &o);
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};

// 远端备份（镜像 desktop BackupController）
class RemoteBackupController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    // 镜像 desktop BackupController 具名属性
    Q_PROPERTY(bool enabled READ enabled NOTIFY dataChanged)
    Q_PROPERTY(int intervalHours READ intervalHours NOTIFY dataChanged)
    Q_PROPERTY(int retain READ retain NOTIFY dataChanged)
    Q_PROPERTY(bool onStart READ onStart NOTIFY dataChanged)
    Q_PROPERTY(QString lastBackup READ lastBackup NOTIFY dataChanged)
public:
    explicit RemoteBackupController(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }
    bool enabled() const { return m_data.value(QStringLiteral("enabled")).toBool(); }
    int intervalHours() const { return m_data.value(QStringLiteral("intervalHours")).toInt(); }
    int retain() const { return m_data.value(QStringLiteral("retain")).toInt(); }
    bool onStart() const { return m_data.value(QStringLiteral("onStart")).toBool(); }
    QString lastBackup() const { return m_data.value(QStringLiteral("lastBackup")).toString(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &field, const QVariant &value);
    Q_INVOKABLE void backupNow(const QString &name, const QString &path);
signals:
    void dataChanged();
private:
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};

// 远端定时任务（镜像 desktop SchedulerController）
class RemoteSchedulerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY dataChanged)
public:
    explicit RemoteSchedulerController(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }
    QVariantList tasks() const { return m_data.value(QStringLiteral("tasks")).toList(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addTask(const QVariantMap &task);
    Q_INVOKABLE void updateTask(int index, const QVariantMap &task);
    Q_INVOKABLE void removeTask(int index);
    Q_INVOKABLE void runTask(int index);
signals:
    void dataChanged();
private:
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};

// 远端核心更新（镜像 desktop UpdateController）
class RemoteUpdateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
public:
    explicit RemoteUpdateController(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void checkUpdate(const QString &name, const QString &type, const QString &version);
    Q_INVOKABLE void updateJar(const QString &name, const QString &path, const QString &type, const QString &version);
signals:
    void dataChanged();
private:
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};
