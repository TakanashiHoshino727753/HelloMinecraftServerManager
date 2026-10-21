#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QAbstractListModel>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
class RemoteClient;
class RemoteDownloadManager;
class RemoteLocalDownloader;

// 当前下载列表子模型（镜像 desktop DownloadManager.downloadList）：供 DownloadsPanel 绑定 count 与方法
class RemoteDownloadList : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    explicit RemoteDownloadList(RemoteDownloadManager *mgr, QObject *parent = nullptr);
    int count() const;
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void pauseAt(int i);
    Q_INVOKABLE void resumeAt(int i);
    Q_INVOKABLE void cancelAt(int i);
    Q_INVOKABLE void restartAt(int i);
    Q_INVOKABLE void removeAt(int i);
signals:
    void changed();
private:
    RemoteDownloadManager *m_mgr;
};

// 远端下载管理（镜像 desktop DownloadManager：当前下载列表）
class RemoteDownloadManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsChanged)
    Q_PROPERTY(QObject *downloadList READ downloadList CONSTANT)
public:
    explicit RemoteDownloadManager(RemoteClient *rc, QObject *parent = nullptr);
    QVariantList downloads() const { return m_list; }
    QObject *downloadList() const { return m_dlList; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void pause(int row) const;
    Q_INVOKABLE void resume(int row) const;
    Q_INVOKABLE void cancel(int row) const;
    Q_INVOKABLE void restart(int row) const;
    Q_INVOKABLE void remove(int row) const;
    Q_INVOKABLE void clear() const;
signals:
    void downloadsChanged();
private:
    void setList(const QVariantList &v);
    RemoteClient *m_rc;
    QVariantList m_list;
    RemoteDownloadList *m_dlList = nullptr;
    QTimer *m_timer;
};

// 远端下载目录（镜像 desktop DownloadCatalog：可下载项 + 一键下载）
class RemoteDownloadCatalog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString currentKey READ currentKey WRITE setCurrentKey NOTIFY currentKeyChanged)
    Q_PROPERTY(QString serverType READ serverType WRITE setServerType NOTIFY serverTypeChanged)
    Q_PROPERTY(QString saveDir READ saveDir WRITE setSaveDir NOTIFY saveDirChanged)
    Q_PROPERTY(QString modVersion READ modVersion WRITE setModVersion NOTIFY modVersionChanged)
    Q_PROPERTY(QStringList selectedLoaders READ selectedLoaders WRITE setSelectedLoaders NOTIFY selectedLoadersChanged)
    Q_PROPERTY(QStringList mcReleases READ mcReleases NOTIFY mcReleasesChanged)
    Q_PROPERTY(bool hasTempJava READ hasTempJava NOTIFY hasTempJavaChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList optimods READ optimods NOTIFY optimodsChanged)
    // 下载目标：local=下载到本机（远控端电脑）；remote=下载到远端被控服务器（默认，走 API）
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged)
    // —— 镜像 desktop DownloadCatalog 具名属性 ——
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString modLoaderType READ modLoaderType WRITE setModLoaderType NOTIFY modLoaderTypeChanged)
public:
    explicit RemoteDownloadCatalog(RemoteClient *rc, QObject *parent = nullptr);
    QString status() const { return m_status; }
    bool loading() const { return m_loading; }
    QString currentKey() const { return m_key; }
    QString serverType() const { return m_type; }
    QString saveDir() const { return m_save; }
    QString modVersion() const { return m_mod; }
    QStringList selectedLoaders() const { return m_loaders; }
    QStringList mcReleases() const { return m_releases; }
    bool hasTempJava() const { return m_temp; }
    QVariantList items() const { return m_items; }
    QVariantList optimods() const { return m_opt; }
    QString language() const { return m_language; }
    QString searchText() const { return m_search; }
    QString modLoaderType() const { return m_loaders.isEmpty() ? QString() : m_loaders.first(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void download(int index) const;
    Q_INVOKABLE void pause(int index) const;
    Q_INVOKABLE void resume(int index) const;
    Q_INVOKABLE void cancel(int index) const;
    Q_INVOKABLE void downloadSelectedLoaders() const;
    Q_INVOKABLE void downloadLoader(int index) const { download(index); }
    Q_INVOKABLE void cleanupTempJava() const;
    Q_INVOKABLE void fetchOptimizationMods(const QString &mc, const QString &loader);
    Q_INVOKABLE QString loaderLabel(const QString &l) const;
    Q_INVOKABLE bool loaderCompatible(const QString &l, const QString &mc) const;
    void setCurrentKey(const QString &k) { if (m_key != k) { m_key = k; emit currentKeyChanged(); refresh(); } }
    void setServerType(const QString &t) { if (m_type != t) { m_type = t; emit serverTypeChanged(); refresh(); } }
    void setSaveDir(const QString &d) { if (m_save != d) { m_save = d; emit saveDirChanged(); } }
    void setModVersion(const QString &v) { if (m_mod != v) { m_mod = v; emit modVersionChanged(); refresh(); } }
    void setSelectedLoaders(const QStringList &l) { if (m_loaders != l) { m_loaders = l; emit selectedLoadersChanged(); } }
    void setLanguage(const QString &l) { if (m_language != l) { m_language = l; emit languageChanged(); refresh(); } }
    void setSearchText(const QString &s) { if (m_search != s) { m_search = s; emit searchTextChanged(); refresh(); } }
    void setModLoaderType(const QString &v) { setSelectedLoaders(v.isEmpty() ? QStringList() : QStringList{v}); }
    Q_INVOKABLE void toggleLoader(const QString &l);
    void setLocalDownloader(RemoteLocalDownloader *ld) { m_local = ld; }
    QString target() const { return m_target; }
    void setTarget(const QString &t) {
        if (m_target != t) {
            m_target = t;
            QSettings().setValue(QStringLiteral("remote/downloadTarget"), t);
            emit targetChanged();
        }
    }
signals:
    void statusChanged(); void loadingChanged(); void currentKeyChanged(); void serverTypeChanged();
    void saveDirChanged(); void modVersionChanged(); void selectedLoadersChanged(); void mcReleasesChanged();
    void hasTempJavaChanged(); void itemsChanged(); void optimodsChanged();
    void languageChanged(); void searchTextChanged(); void modLoaderTypeChanged();
    void targetChanged();
private:
    RemoteClient *m_rc;
    QString m_status; bool m_loading = false; QString m_key, m_type, m_save, m_mod;
    QStringList m_loaders, m_releases; bool m_temp = false;
    QVariantList m_items, m_opt;
    QString m_language; QString m_search;
    QString m_target = QStringLiteral("remote");
    RemoteLocalDownloader *m_local = nullptr;
    void setStatus(const QString &v);
    void setLoading(bool v);
    void setItems(const QVariantList &v);
    void setHasTempJava(bool v);
    void setOptimods(const QVariantList &v);
};

// 远端 Java 管理（镜像 desktop JavaManager：按版本确保 Java）
class RemoteJavaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool hasTempJava READ hasTempJava NOTIFY hasTempJavaChanged)
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    Q_PROPERTY(QString manualJavaHome READ manualJavaHome WRITE setManualJavaHome NOTIFY manualJavaHomeChanged)
public:
    explicit RemoteJavaManager(RemoteClient *rc, QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString statusText() const { return m_status; }
    QString errorText() const { return m_error; }
    bool hasTempJava() const { return m_temp; }
    QVariantMap info() const { return m_info; }
    QString manualJavaHome() const { return m_manualHome; }
    Q_INVOKABLE void ensure(const QString &mc);
    Q_INVOKABLE void resolve(const QString &mc);
    Q_INVOKABLE static QString requiredFeature(const QString &mc);
    void setManualJavaHome(const QString &p) { if (m_manualHome != p) { m_manualHome = p; emit manualJavaHomeChanged(); } }
signals:
    void busyChanged(); void progressChanged(); void statusTextChanged();
    void errorTextChanged(); void hasTempJavaChanged(); void infoChanged();
    void manualJavaHomeChanged();
private:
    void parse(const QJsonObject &o);
    RemoteClient *m_rc;
    bool m_busy = false; qreal m_progress = 0; QString m_status, m_error;
    bool m_temp = false; QVariantMap m_info;
    QString m_manualHome;
};

// 远控端“本机下载”任务列表模型：直连模组源把文件落在本机，供 DownloadsPanel 绑定。
// 角色与桌面 DownloadListModel 完全一致（title/percent/state/errorText/path/url），
// 因此下载面板可无缝切换远端/本机两个列表。
class RemoteLocalDownloadList : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { TitleRole = Qt::UserRole + 1, PercentRole, StateRole, TaskIdRole, ErrorRole, PathRole, UrlRole };
    explicit RemoteLocalDownloadList(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &p = QModelIndex()) const override { Q_UNUSED(p); return m_rows.size(); }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || i.row() < 0 || i.row() >= m_rows.size()) return {};
        const Row &r = m_rows.at(i.row());
        switch (role) {
            case TitleRole:   return r.title;
            case PercentRole: return r.percent;
            case StateRole:   return r.state;
            case TaskIdRole:  return r.id;
            case ErrorRole:   return r.error;
            case PathRole:    return r.path;
            case UrlRole:     return r.url;
        }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{TitleRole, "title"}, {PercentRole, "percent"}, {StateRole, "state"},
                {TaskIdRole, "taskId"}, {ErrorRole, "errorText"}, {PathRole, "path"}, {UrlRole, "url"}};
    }
    int count() const { return m_rows.size(); }

    void add(const QString &id, const QString &title, const QString &path, const QString &url) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
        m_rows.append({id, title, QString(), path, url, 0, QStringLiteral("downloading")});
        endInsertRows(); emit countChanged();
    }
    void setProgress(const QString &id, qreal p) {
        const int r = findRow(id); if (r < 0) return;
        m_rows[r].percent = p; emit dataChanged(index(r), index(r), {PercentRole});
    }
    void setState(const QString &id, const QString &state, const QString &err = QString()) {
        const int r = findRow(id); if (r < 0) return;
        m_rows[r].state = state; m_rows[r].error = err;
        if (state == QStringLiteral("done")) m_rows[r].percent = 100;
        emit dataChanged(index(r), index(r));
    }
    void removeAt(int row) {
        if (row < 0 || row >= m_rows.size()) return;
        beginRemoveRows(QModelIndex(), row, row); m_rows.removeAt(row); endRemoveRows(); emit countChanged();
    }
    void clearFinished() {
        for (int i = m_rows.size() - 1; i >= 0; --i) {
            const QString s = m_rows.at(i).state;
            if (s == QStringLiteral("done") || s == QStringLiteral("error") || s == QStringLiteral("canceled")) {
                beginRemoveRows(QModelIndex(), i, i); m_rows.removeAt(i); endRemoveRows();
            }
        }
        emit countChanged();
    }
    QString urlForId(const QString &id) const { const int r = findRow(id); return r < 0 ? QString() : m_rows.at(r).url; }
    QString pathForId(const QString &id) const { const int r = findRow(id); return r < 0 ? QString() : m_rows.at(r).path; }
    QString titleForId(const QString &id) const { const int r = findRow(id); return r < 0 ? QString() : m_rows.at(r).title; }

    Q_INVOKABLE void cancelAt(int row) { if (row >= 0 && row < m_rows.size()) emit cancelRequested(m_rows.at(row).id); }
    Q_INVOKABLE void pauseAt(int row) { if (row >= 0 && row < m_rows.size()) emit pauseRequested(m_rows.at(row).id); }
    Q_INVOKABLE void resumeAt(int row) { if (row >= 0 && row < m_rows.size()) emit resumeRequested(m_rows.at(row).id); }
    Q_INVOKABLE void restartAt(int row) { if (row >= 0 && row < m_rows.size()) emit restartRequested(m_rows.at(row).id); }
signals:
    void cancelRequested(const QString &id);
    void pauseRequested(const QString &id);
    void resumeRequested(const QString &id);
    void restartRequested(const QString &id);
    void countChanged();
private:
    struct Row { QString id, title, error, path, url; qreal percent = 0; QString state; };
    int findRow(const QString &id) const {
        for (int i = 0; i < m_rows.size(); ++i) if (m_rows.at(i).id == id) return i;
        return -1;
    }
    QList<Row> m_rows;
};

// 远控端“本机下载”管理器：直接拉取模组源 URL 落在本机目录，支持进度/暂停(断点续传)/取消。
// 与桌面 DownloadManager 能力对齐，但独立运行在远控端机器上（下载目标=本机）。
class RemoteLocalDownloader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *downloadList READ downloadList CONSTANT)
    Q_PROPERTY(QString saveDir READ saveDir WRITE setSaveDir NOTIFY saveDirChanged)
public:
    explicit RemoteLocalDownloader(QObject *parent = nullptr);
    RemoteLocalDownloadList *downloadList() const { return m_list; }
    QString saveDir() const { return m_save; }
    void setSaveDir(const QString &d) {
        if (m_save != d) { m_save = d; QSettings().setValue(QStringLiteral("remote/downloadDir"), d); emit saveDirChanged(); }
    }
    Q_INVOKABLE void download(const QString &url, const QString &dir, const QString &filename, const QString &title);
    Q_INVOKABLE void pause(const QString &id);
    Q_INVOKABLE void resume(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void restart(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void clear();
signals:
    void saveDirChanged();
private:
    void startTask(const QString &id, const QString &url, const QString &path, const QString &title, qint64 offset);
    void beginDownload(const QString &id, const QUrl &url, qint64 offset);
    void onFinished(const QString &id);
    void dropTask(const QString &id, bool removeFile);

    RemoteLocalDownloadList *m_list = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_save;
    struct Task {
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
        QString path, url;
        qint64 offset = 0, total = -1;
        int redirects = 0;
        bool paused = false;
    };
    QHash<QString, Task> m_tasks;
    static int nextSeq() { static int s = 0; return ++s; }
};

