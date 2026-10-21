#include "remotedownload.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QUrl>
#include <QTimer>
#include <QStandardPaths>

// —— RemoteDownloadList：镜像 desktop DownloadManager.downloadList 子模型 ——
RemoteDownloadList::RemoteDownloadList(RemoteDownloadManager *mgr, QObject *parent)
    : QObject(parent), m_mgr(mgr) {}

int RemoteDownloadList::count() const
{
    return m_mgr ? m_mgr->downloads().size() : 0;
}

void RemoteDownloadList::clearFinished()
{
    if (!m_mgr) return;
    QVariantList l = m_mgr->downloads();
    for (int i = l.size() - 1; i >= 0; --i) {
        const QVariantMap m = l.at(i).toMap();
        const QString s = m.value(QStringLiteral("state")).toString().toLower();
        if (s == QStringLiteral("finished") || s == QStringLiteral("completed")
            || s == QStringLiteral("done") || s == QStringLiteral("success"))
            m_mgr->remove(i);
    }
}
void RemoteDownloadList::pauseAt(int i) { if (m_mgr) m_mgr->pause(i); }
void RemoteDownloadList::resumeAt(int i) { if (m_mgr) m_mgr->resume(i); }
void RemoteDownloadList::cancelAt(int i) { if (m_mgr) m_mgr->cancel(i); }
void RemoteDownloadList::restartAt(int i) { if (m_mgr) m_mgr->restart(i); }
void RemoteDownloadList::removeAt(int i) { if (m_mgr) m_mgr->remove(i); }

RemoteDownloadManager::RemoteDownloadManager(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc), m_dlList(new RemoteDownloadList(this, this))
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &RemoteDownloadManager::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
    connect(this, &RemoteDownloadManager::downloadsChanged, m_dlList, &RemoteDownloadList::changed);
}

void RemoteDownloadManager::setList(const QVariantList &v)
{
    if (v == m_list) return;
    m_list = v;
    emit downloadsChanged();
}

void RemoteDownloadManager::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/downloads"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QByteArray ba = r->readAll();
            if (!ba.isEmpty())
                setList(QJsonDocument::fromJson(ba).object().value(QStringLiteral("downloads")).toArray().toVariantList());
        }
        r->deleteLater();
    });
}

void RemoteDownloadManager::pause(int row) const { m_rc->apiGet("/downloads/pause", {{"row", row}}); }
void RemoteDownloadManager::resume(int row) const { m_rc->apiGet("/downloads/resume", {{"row", row}}); }
void RemoteDownloadManager::cancel(int row) const { m_rc->apiGet("/downloads/cancel", {{"row", row}}); }
void RemoteDownloadManager::restart(int row) const { m_rc->apiGet("/downloads/restart", {{"row", row}}); }
void RemoteDownloadManager::remove(int row) const { m_rc->apiGet("/downloads/remove", {{"row", row}}); }
void RemoteDownloadManager::clear() const { m_rc->apiGet(QStringLiteral("/downloads/clear")); }

RemoteDownloadCatalog::RemoteDownloadCatalog(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() { if (m_rc->connected()) refresh(); });
}

void RemoteDownloadCatalog::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/catalog"),
        {{QStringLiteral("cat"), m_key}, {QStringLiteral("type"), m_type}});
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            setStatus(o.value(QStringLiteral("status")).toString());
            setLoading(o.value(QStringLiteral("loading")).toBool());
            setItems(o.value(QStringLiteral("items")).toArray().toVariantList());
            setHasTempJava(o.value(QStringLiteral("hasTempJava")).toBool());
        }
        r->deleteLater();
    });
}

void RemoteDownloadCatalog::setStatus(const QString &v) { if (v != m_status) { m_status = v; emit statusChanged(); } }
void RemoteDownloadCatalog::setLoading(bool v) { if (v != m_loading) { m_loading = v; emit loadingChanged(); } }
void RemoteDownloadCatalog::setItems(const QVariantList &v) { if (v != m_items) { m_items = v; emit itemsChanged(); } }
void RemoteDownloadCatalog::setHasTempJava(bool v) { if (v != m_temp) { m_temp = v; emit hasTempJavaChanged(); } }

void RemoteDownloadCatalog::download(int index) const
{
    if (index < 0 || index >= m_items.size()) return;
    // 下载到本机：直接拉取模组源 URL（无需经过被控端）
    if (m_target == QStringLiteral("local")) {
        if (m_local) {
            const QVariantMap it = m_items.at(index).toMap();
            const QString url = it.value(QStringLiteral("url")).toString();
            const QString title = it.value(QStringLiteral("title")).toString();
            const QString filename = it.value(QStringLiteral("filename")).toString();
            if (!url.isEmpty())
                m_local->download(url, m_save, filename, title);
        }
        return;
    }
    // 下载到远端被控服务器（默认）：由被控端执行下载
    m_rc->apiGet(QStringLiteral("/catalog/download"), {{QStringLiteral("index"), index}});
}
void RemoteDownloadCatalog::pause(int index) const { m_rc->apiGet("/catalog/pause", {{"index", index}}); }
void RemoteDownloadCatalog::resume(int index) const { m_rc->apiGet("/catalog/resume", {{"index", index}}); }
void RemoteDownloadCatalog::cancel(int index) const { m_rc->apiGet("/catalog/cancel", {{"index", index}}); }
void RemoteDownloadCatalog::downloadSelectedLoaders() const { for (const QString &i : m_loaders) download(i.toInt()); }
void RemoteDownloadCatalog::cleanupTempJava() const { m_rc->apiGet(QStringLiteral("/catalog/cleantempjava")); }
void RemoteDownloadCatalog::toggleLoader(const QString &l)
{
    QStringList s = m_loaders;
    if (s.contains(l)) s.removeAll(l); else s.append(l);
    setSelectedLoaders(s);
}
void RemoteDownloadCatalog::fetchOptimizationMods(const QString &mc, const QString &loader)
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/optimods"), {{QStringLiteral("mc"), mc}, {QStringLiteral("loader"), loader}});
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError)
            setOptimods(QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("mods")).toArray().toVariantList());
        r->deleteLater();
    });
}
void RemoteDownloadCatalog::setOptimods(const QVariantList &v) { if (v != m_opt) { m_opt = v; emit optimodsChanged(); } }

QString RemoteDownloadCatalog::loaderLabel(const QString &l) const
{
    static const QHash<QString, QString> map{
        {QStringLiteral("forge"), QStringLiteral("Forge")},
        {QStringLiteral("fabric"), QStringLiteral("Fabric")},
        {QStringLiteral("neoforge"), QStringLiteral("NeoForge")},
        {QStringLiteral("quilt"), QStringLiteral("Quilt")},
        {QStringLiteral("paper"), QStringLiteral("Paper")},
        {QStringLiteral("spigot"), QStringLiteral("Spigot")},
        {QStringLiteral("purpur"), QStringLiteral("Purpur")}};
    return map.value(l.toLower(), l);
}
bool RemoteDownloadCatalog::loaderCompatible(const QString &l, const QString &mc) const
{
    Q_UNUSED(l); Q_UNUSED(mc);
    return true;   // 远控端无本地版本矩阵，默认兼容（服务端下载时会二次校验）
}

RemoteJavaManager::RemoteJavaManager(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() { if (m_rc->connected()) resolve(QString()); });
}

QString RemoteJavaManager::requiredFeature(const QString &mc)
{
    double v = mc.toDouble();
    if (v >= 1.21) return QStringLiteral("java21");
    if (v >= 1.18) return QStringLiteral("java17");
    if (v >= 1.12) return QStringLiteral("java11");
    return QStringLiteral("java8");
}

void RemoteJavaManager::parse(const QJsonObject &o)
{
    const bool busy = o.value(QStringLiteral("busy")).toBool(); if (busy != m_busy) { m_busy = busy; emit busyChanged(); }
    const qreal p = o.value(QStringLiteral("progress")).toDouble(); if (qAbs(p - m_progress) > 1e-4) { m_progress = p; emit progressChanged(); }
    const QString s = o.value(QStringLiteral("statusText")).toString(); if (s != m_status) { m_status = s; emit statusTextChanged(); }
    const QString e = o.value(QStringLiteral("errorText")).toString(); if (e != m_error) { m_error = e; emit errorTextChanged(); }
    const bool t = o.value(QStringLiteral("hasTempJava")).toBool(); if (t != m_temp) { m_temp = t; emit hasTempJavaChanged(); }
    m_info = o.toVariantMap();
    emit infoChanged();
}

void RemoteJavaManager::ensure(const QString &mc)
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/java/download"), {{QStringLiteral("mc"), mc}});
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) parse(QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    });
}

void RemoteJavaManager::resolve(const QString &mc)
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/java"), {{QStringLiteral("mc"), mc}});
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) parse(QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    });
}

// ---------------- 远控端“本机下载”管理器 ----------------
RemoteLocalDownloader::RemoteLocalDownloader(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_list = new RemoteLocalDownloadList(this);
    m_save = QSettings().value(QStringLiteral("remote/downloadDir"), QString()).toString();
    if (m_save.isEmpty()) {
#ifdef Q_OS_LINUX
        m_save = QDir::homePath() + QStringLiteral("/HMCSM/Downloads");
#else
        m_save = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/HMCSM");
#endif
    }
    connect(m_list, &RemoteLocalDownloadList::cancelRequested, this, &RemoteLocalDownloader::cancel);
    connect(m_list, &RemoteLocalDownloadList::pauseRequested, this, &RemoteLocalDownloader::pause);
    connect(m_list, &RemoteLocalDownloadList::resumeRequested, this, &RemoteLocalDownloader::resume);
    connect(m_list, &RemoteLocalDownloadList::restartRequested, this, &RemoteLocalDownloader::restart);
}

void RemoteLocalDownloader::download(const QString &url, const QString &dir, const QString &filename, const QString &title)
{
    QDir().mkpath(dir);
    const QString name = filename.isEmpty() ? QUrl(url).fileName() : filename;
    const QString filePath = QDir(dir).filePath(name);
    const QString id = QString::number(nextSeq()) + QStringLiteral("-") + name;
    m_list->add(id, title.isEmpty() ? name : title, filePath, url);
    startTask(id, url, filePath, title.isEmpty() ? name : title, 0);
}

void RemoteLocalDownloader::startTask(const QString &id, const QString &url, const QString &path,
                                       const QString &title, qint64 offset)
{
    auto it = m_tasks.find(id);
    if (it != m_tasks.end()) {
        Task old = it.value();
        m_tasks.erase(it);
        if (old.reply) { old.reply->abort(); old.reply->deleteLater(); }
        if (old.file) { if (old.file->isOpen()) old.file->close(); delete old.file; }
    }
    Task t;
    t.path = path; t.url = url; t.offset = offset; t.total = -1; t.redirects = 0; t.paused = false;
    t.file = new QFile(path);
    m_tasks.insert(id, t);
    m_list->setState(id, QStringLiteral("downloading"));
    m_list->setProgress(id, 0);
    beginDownload(id, QUrl(url), offset);
}

void RemoteLocalDownloader::beginDownload(const QString &id, const QUrl &url, qint64 offset)
{
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return;
    Task &t = it.value();
    t.url = url.toString();

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(90000);   // 文件下载：90s 无数据传输则报错，避免无限卡死
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HMCSM-Remote/1.0"));
    if (offset > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");

    QNetworkReply *reply = m_nam->get(req);
    // 总超时兜底：连接建立阶段卡死（被墙域名 SYN 黑洞）时强制结束，避免无限“连接中”
    QTimer *guard = new QTimer(reply);
    guard->setSingleShot(true);
    guard->setInterval(90000);
    connect(guard, &QTimer::timeout, reply, [this, reply, id]() {
        if (!reply->isRunning()) return;
        m_list->setState(id, QStringLiteral("error"), QStringLiteral("下载超时（90s 无响应，可能源站不可达）"));
        dropTask(id, false);
    });
    guard->start();

    t.reply = reply;
    t.offset = offset;

    bool opened = false;
    if (offset == 0) {
        opened = t.file->open(QIODevice::WriteOnly | QIODevice::Truncate);
    } else {
        opened = t.file->open(QIODevice::ReadWrite) && t.file->seek(offset);
    }
    if (!opened) {
        m_list->setState(id, QStringLiteral("error"), t.file->errorString());
        dropTask(id, false);
        return;
    }

    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 recv, qint64 total) {
        auto it2 = m_tasks.find(id);
        if (it2 == m_tasks.end()) return;
        Task &tk = it2.value();
        qint64 full = -1;
        if (tk.reply && tk.reply->hasRawHeader("Content-Range")) {
            QByteArray cr = tk.reply->rawHeader("Content-Range");
            int slash = cr.lastIndexOf('/');
            if (slash >= 0) full = cr.mid(slash + 1).toLongLong();
        } else if (total > 0) {
            full = total;
        }
        if (full > 0) tk.total = full;
        const qint64 cur = tk.offset + recv;
        if (tk.total > 0)
            m_list->setProgress(id, qMin(100.0, cur * 100.0 / tk.total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, id]() { onFinished(id); });
}

void RemoteLocalDownloader::onFinished(const QString &id)
{
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return;
    Task &t = it.value();
    if (t.paused) return;

    const QVariant redir = t.reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    const bool noErr = t.reply->error() == QNetworkReply::NoError;
    const bool isRedir = !redir.isNull();

    if (isRedir && t.redirects < 5 && noErr) {
        const QUrl next = t.reply->url().resolved(redir.toUrl());
        t.redirects++;
        t.reply->deleteLater();
        beginDownload(id, next, t.offset);
        return;
    }
    const bool isPartial = t.reply->hasRawHeader("Content-Range");
    if (t.offset > 0 && !isPartial) {   // 请求了续传但服务器未支持：从头全量重下
        t.reply->deleteLater();
        beginDownload(id, QUrl(t.url), 0);
        return;
    }
    if (isRedir) { m_list->setState(id, QStringLiteral("error"), QStringLiteral("重定向次数过多")); dropTask(id, false); return; }
    if (!noErr) { m_list->setState(id, QStringLiteral("error"), t.reply->errorString()); dropTask(id, false); return; }

    const QByteArray data = t.reply->readAll();
    if (!t.file->isOpen()) t.file->open(QIODevice::ReadWrite);
    if (t.file->write(data) != data.size()) {
        m_list->setState(id, QStringLiteral("error"), t.file->errorString());
        dropTask(id, false);
        return;
    }
    t.file->flush();
    t.file->close();
    delete t.file;
    t.file = nullptr;
    t.reply->deleteLater();
    const QString path = t.path;          // 复制到局部：erase 后 t 悬垂
    m_tasks.erase(it);
    Q_UNUSED(path);
    m_list->setState(id, QStringLiteral("done"));
}

void RemoteLocalDownloader::dropTask(const QString &id, bool removeFile)
{
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return;
    Task t = it.value();
    m_tasks.erase(it);
    if (t.reply) { t.reply->abort(); t.reply->deleteLater(); }
    if (t.file) {
        if (t.file->isOpen()) t.file->close();
        if (removeFile) t.file->remove();
        delete t.file;
    }
}

void RemoteLocalDownloader::pause(const QString &id)
{
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return;
    Task &t = it.value();
    if (!t.reply) return;
    t.paused = true;
    t.reply->abort();
    t.reply->deleteLater();
    t.reply = nullptr;
    if (t.file) t.file->flush();
    m_list->setState(id, QStringLiteral("paused"));
}

void RemoteLocalDownloader::resume(const QString &id)
{
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return;
    Task &t = it.value();
    if (!t.paused) return;
    const qint64 offset = t.file ? t.file->size() : 0;
    t.paused = false;
    t.total = -1;
    beginDownload(id, QUrl(t.url), offset);
    m_list->setState(id, QStringLiteral("downloading"));
}

void RemoteLocalDownloader::cancel(const QString &id)
{
    dropTask(id, true);
    m_list->setState(id, QStringLiteral("canceled"));
}

void RemoteLocalDownloader::restart(const QString &id)
{
    const QString url = m_list->urlForId(id);
    const QString path = m_list->pathForId(id);
    const QString title = m_list->titleForId(id);
    if (url.isEmpty()) { m_list->setState(id, QStringLiteral("error"), QStringLiteral("无法重试：缺少下载地址")); return; }
    startTask(id, url, path, title, 0);
}

void RemoteLocalDownloader::remove(const QString &id)
{
    dropTask(id, false);
    for (int i = 0; i < m_list->rowCount(); ++i) {
        if (m_list->data(m_list->index(i, 0), RemoteLocalDownloadList::TaskIdRole).toString() == id) {
            m_list->removeAt(i);
            break;
        }
    }
}

void RemoteLocalDownloader::clear()
{
    for (int i = m_list->rowCount() - 1; i >= 0; --i) {
        const QString st = m_list->data(m_list->index(i, 0), RemoteLocalDownloadList::StateRole).toString();
        if (st == QStringLiteral("done") || st == QStringLiteral("error") || st == QStringLiteral("canceled")) {
            const QString id = m_list->data(m_list->index(i, 0), RemoteLocalDownloadList::TaskIdRole).toString();
            dropTask(id, false);
        }
    }
    m_list->clearFinished();
}
