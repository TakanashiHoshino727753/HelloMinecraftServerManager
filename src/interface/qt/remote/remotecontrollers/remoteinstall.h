#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
class RemoteClient;

// 远端安装协调器（镜像 desktop InstallCoordinator：并发安装任务）
class RemoteInstallCoordinator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString stageText READ stageText NOTIFY stageTextChanged)
    Q_PROPERTY(qreal stageProgress READ stageProgress NOTIFY stageProgressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
public:
    explicit RemoteInstallCoordinator(RemoteClient *rc, QObject *parent = nullptr);
    QVariantList tasks() const { return m_tasks; }
    qreal progress() const { return m_progress; }
    QString stageText() const { return m_stage; }
    qreal stageProgress() const { return m_stageP; }
    bool busy() const { return m_busy; }
    QString statusText() const { return m_status; }
    // 镜像 desktop InstallCoordinator.install(type, version, label)（3 参）
    Q_INVOKABLE int install(const QString &type, const QString &version, const QString &label);
    Q_INVOKABLE int installServer(const QString &type, const QString &version, const QStringList &loaders,
                                  const QString &label, bool addToList = true);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearFinished();
signals:
    void tasksChanged(); void progressChanged(); void stageTextChanged();
    void stageProgressChanged(); void busyChanged(); void statusTextChanged();
private:
    void setTasks(const QVariantList &v);
    RemoteClient *m_rc;
    QVariantList m_tasks; qreal m_progress = 0, m_stageP = 0; QString m_stage, m_status; bool m_busy = false;
    QTimer *m_timer;
};

// 远端服务端创建（镜像 desktop CreateServerController）
class RemoteCreateServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList types READ types NOTIFY typesChanged)
    Q_PROPERTY(QStringList versions READ versions NOTIFY versionsChanged)
    Q_PROPERTY(QString currentType READ currentType WRITE setCurrentType NOTIFY currentTypeChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion WRITE setCurrentVersion NOTIFY currentVersionChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString saveDir READ saveDir WRITE setSaveDir NOTIFY saveDirChanged)
    Q_PROPERTY(bool eulaAccepted READ eulaAccepted WRITE setEulaAccepted NOTIFY eulaAcceptedChanged)
    Q_PROPERTY(QStringList selectedLoaders READ selectedLoaders WRITE setSelectedLoaders NOTIFY selectedLoadersChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool done READ done NOTIFY doneChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(qreal stageProgress READ stageProgress NOTIFY stageProgressChanged)
    Q_PROPERTY(QString stageText READ stageText NOTIFY stageTextChanged)
    Q_PROPERTY(QVariantList modLoaders READ modLoaders NOTIFY modLoadersChanged)
public:
    explicit RemoteCreateServer(RemoteClient *rc, QObject *parent = nullptr);
    QStringList types() const { return m_types; }
    QStringList versions() const { return m_versions; }
    QString currentType() const { return m_type; }
    QString currentVersion() const { return m_version; }
    QString name() const { return m_name; }
    QString saveDir() const { return m_save; }
    bool eulaAccepted() const { return m_eula; }
    QStringList selectedLoaders() const { return m_loaders; }
    bool busy() const { return m_busy; }
    bool done() const { return m_done; }
    QString statusText() const { return m_status; }
    qreal progress() const { return m_progress; }
    qreal stageProgress() const { return m_stageP; }
    QString stageText() const { return m_stageT; }
    QVariantList modLoaders() const { return m_modLoaders; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadVersions();
    Q_INVOKABLE void create();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void importZip(const QString &zipPath);
    Q_INVOKABLE QString loaderLabel(const QString &l) const;
    Q_INVOKABLE bool loaderCompatible(const QString &l) const;
    Q_INVOKABLE bool dirOccupied(const QString &dir) const;
    void setCurrentType(const QString &v);
    void setCurrentVersion(const QString &v);
    void setName(const QString &v);
    void setSaveDir(const QString &v);
    void setEulaAccepted(bool v);
    void setSelectedLoaders(const QStringList &v);
signals:
    void typesChanged(); void versionsChanged(); void currentTypeChanged(); void currentVersionChanged();
    void nameChanged(); void saveDirChanged(); void eulaAcceptedChanged(); void selectedLoadersChanged();
    void busyChanged(); void doneChanged(); void statusTextChanged(); void progressChanged();
    void stageProgressChanged(); void stageTextChanged(); void modLoadersChanged();
private:
    RemoteClient *m_rc;
    QStringList m_types, m_versions, m_loaders;
    QVariantList m_modLoaders;
    QString m_type, m_version, m_name, m_save, m_status, m_stageT;
    bool m_eula = false, m_busy = false, m_done = false;
    qreal m_progress = 0, m_stageP = 0;
};

// 远端整合包导入（镜像 desktop ModpackImporter）
class RemoteImportModpack : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString zipPath READ zipPath WRITE setZipPath NOTIFY zipPathChanged)
    Q_PROPERTY(QString targetDir READ targetDir WRITE setTargetDir NOTIFY targetDirChanged)
    Q_PROPERTY(QString modpackName READ modpackName NOTIFY modpackNameChanged)
    Q_PROPERTY(QString detectedType READ detectedType NOTIFY detectedTypeChanged)
    Q_PROPERTY(QString loader READ loader NOTIFY loaderChanged)
    Q_PROPERTY(QString gameVersion READ gameVersion NOTIFY gameVersionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool done READ done NOTIFY doneChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
public:
    explicit RemoteImportModpack(RemoteClient *rc, QObject *parent = nullptr);
    QString zipPath() const { return m_zip; }
    QString targetDir() const { return m_dir; }
    QString modpackName() const { return m_name; }
    QString detectedType() const { return m_type; }
    QString loader() const { return m_loader; }
    QString gameVersion() const { return m_ver; }
    QString statusText() const { return m_status; }
    bool busy() const { return m_busy; }
    bool done() const { return m_done; }
    qreal progress() const { return m_progress; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void import();
    Q_INVOKABLE void reset();
    void setZipPath(const QString &v) { if (m_zip != v) { m_zip = v; emit zipPathChanged(); refresh(); } }
    void setTargetDir(const QString &v) { if (m_dir != v) { m_dir = v; emit targetDirChanged(); } }
signals:
    void zipPathChanged(); void targetDirChanged(); void modpackNameChanged(); void detectedTypeChanged();
    void loaderChanged(); void gameVersionChanged(); void statusTextChanged(); void busyChanged();
    void doneChanged(); void progressChanged();
private:
    RemoteClient *m_rc;
    QString m_zip, m_dir, m_name, m_type, m_loader, m_ver, m_status;
    bool m_busy = false, m_done = false; qreal m_progress = 0;
};
