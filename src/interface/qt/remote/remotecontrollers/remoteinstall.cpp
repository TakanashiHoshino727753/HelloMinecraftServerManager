#include "remoteinstall.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <algorithm>

RemoteInstallCoordinator::RemoteInstallCoordinator(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &RemoteInstallCoordinator::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}

void RemoteInstallCoordinator::setTasks(const QVariantList &v)
{
    if (v == m_tasks) return;
    m_tasks = v;
    emit tasksChanged();
    const bool busy = std::any_of(v.begin(), v.end(), [](const QVariant &x) {
        return x.toMap().value(QStringLiteral("busy")).toBool();
    });
    if (busy != m_busy) { m_busy = busy; emit busyChanged(); }
}

void RemoteInstallCoordinator::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/installtasks"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QVariantList tasks = QJsonDocument::fromJson(r->readAll()).object()
                                           .value(QStringLiteral("tasks")).toArray().toVariantList();
            setTasks(tasks);
            QVariantMap focus = tasks.isEmpty() ? QVariantMap() : tasks.first().toMap();
            const qreal p = focus.value(QStringLiteral("progress")).toDouble();
            if (qAbs(p - m_progress) > 1e-4) { m_progress = p; emit progressChanged(); }
            const QString s = focus.value(QStringLiteral("stageText")).toString();
            if (s != m_stage) { m_stage = s; emit stageTextChanged(); }
            const QString st = focus.value(QStringLiteral("statusText")).toString();
            if (st != m_status) { m_status = st; emit statusTextChanged(); }
        }
        r->deleteLater();
    });
}

int RemoteInstallCoordinator::installServer(const QString &type, const QString &version,
                                            const QStringList &loaders, const QString &label, bool addToList)
{
    QJsonArray arr; for (const QString &l : loaders) arr.append(l);
    m_rc->apiPost(QStringLiteral("/installserver"),
                  QJsonObject{{QStringLiteral("type"), type},
                              {QStringLiteral("version"), version},
                              {QStringLiteral("loaders"), arr},
                              {QStringLiteral("label"), label},
                              {QStringLiteral("addToList"), addToList}});
    refresh();
    return -1;
}

int RemoteInstallCoordinator::install(const QString &type, const QString &version, const QString &label)
{
    return installServer(type, version, QStringList(), label, true);
}

void RemoteInstallCoordinator::clearFinished()
{
    m_rc->apiDelete(QStringLiteral("/installtasks"));
    refresh();
}

RemoteCreateServer::RemoteCreateServer(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() { if (m_rc->connected()) refresh(); });
}

void RemoteCreateServer::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/create/progress"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            const QStringList types = o.value(QStringLiteral("types")).toVariant().toStringList();
            if (types != m_types) { m_types = types; emit typesChanged(); }
            const QStringList versions = o.value(QStringLiteral("versions")).toVariant().toStringList();
            if (versions != m_versions) { m_versions = versions; emit versionsChanged(); }
            const QString ct = o.value(QStringLiteral("currentType")).toString();
            if (ct != m_type) { m_type = ct; emit currentTypeChanged(); }
            const QString cv = o.value(QStringLiteral("currentVersion")).toString();
            if (cv != m_version) { m_version = cv; emit currentVersionChanged(); }
            const QString nm = o.value(QStringLiteral("name")).toString();
            if (nm != m_name) { m_name = nm; emit nameChanged(); }
            const QString sd = o.value(QStringLiteral("saveDir")).toString();
            if (sd != m_save) { m_save = sd; emit saveDirChanged(); }
            const bool eu = o.value(QStringLiteral("eulaAccepted")).toBool();
            if (eu != m_eula) { m_eula = eu; emit eulaAcceptedChanged(); }
            const QStringList sl = o.value(QStringLiteral("selectedLoaders")).toVariant().toStringList();
            if (sl != m_loaders) { m_loaders = sl; emit selectedLoadersChanged(); }
            const QVariantList ml = o.value(QStringLiteral("modLoaders")).toArray().toVariantList();
            if (ml != m_modLoaders) { m_modLoaders = ml; emit modLoadersChanged(); }
            const bool busy = o.value(QStringLiteral("busy")).toBool();
            if (busy != m_busy) { m_busy = busy; emit busyChanged(); }
            const bool done = o.value(QStringLiteral("done")).toBool();
            if (done != m_done) { m_done = done; emit doneChanged(); }
            const QString st = o.value(QStringLiteral("statusText")).toString();
            if (st != m_status) { m_status = st; emit statusTextChanged(); }
            const qreal p = o.value(QStringLiteral("progress")).toDouble();
            if (qAbs(p - m_progress) > 1e-4) { m_progress = p; emit progressChanged(); }
            const qreal sp = o.value(QStringLiteral("stageProgress")).toDouble();
            if (qAbs(sp - m_stageP) > 1e-4) { m_stageP = sp; emit stageProgressChanged(); }
            const QString stt = o.value(QStringLiteral("stageText")).toString();
            if (stt != m_stageT) { m_stageT = stt; emit stageTextChanged(); }
        }
        r->deleteLater();
    });
}

void RemoteCreateServer::setCurrentType(const QString &v)
{
    if (m_type == v) return;
    m_type = v; emit currentTypeChanged(); loadVersions();
}
void RemoteCreateServer::setCurrentVersion(const QString &v) { if (m_version != v) { m_version = v; emit currentVersionChanged(); } }
void RemoteCreateServer::setName(const QString &v) { if (m_name != v) { m_name = v; emit nameChanged(); } }
void RemoteCreateServer::setSaveDir(const QString &v) { if (m_save != v) { m_save = v; emit saveDirChanged(); } }
void RemoteCreateServer::setEulaAccepted(bool v) { if (m_eula != v) { m_eula = v; emit eulaAcceptedChanged(); } }
void RemoteCreateServer::setSelectedLoaders(const QStringList &v) { if (m_loaders != v) { m_loaders = v; emit selectedLoadersChanged(); } }

void RemoteCreateServer::loadVersions()
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/create/loadversions"), QJsonObject{{QStringLiteral("type"), m_type}});
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}

void RemoteCreateServer::create()
{
    QJsonArray arr; for (const QString &l : m_loaders) arr.append(l);
    QJsonObject body{{QStringLiteral("currentType"), m_type},
                     {QStringLiteral("currentVersion"), m_version},
                     {QStringLiteral("name"), m_name},
                     {QStringLiteral("saveDir"), m_save},
                     {QStringLiteral("eulaAccepted"), m_eula},
                     {QStringLiteral("selectedLoaders"), arr}};
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/create"), body);
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}

void RemoteCreateServer::reset()
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/create/reset"), QJsonObject());
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}

void RemoteCreateServer::importZip(const QString &zipPath)
{
    QJsonObject body{{QStringLiteral("importMode"), true},
                     {QStringLiteral("zipPath"), zipPath},
                     {QStringLiteral("targetDir"), m_save},
                     {QStringLiteral("name"), m_name}};
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/create"), body);
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}

QString RemoteCreateServer::loaderLabel(const QString &l) const
{
    for (const QVariant &v : m_modLoaders)
        if (v.toMap().value(QStringLiteral("key")).toString() == l)
            return v.toMap().value(QStringLiteral("label")).toString();
    return l;
}
bool RemoteCreateServer::loaderCompatible(const QString &l) const
{
    for (const QVariant &v : m_modLoaders)
        if (v.toMap().value(QStringLiteral("key")).toString() == l)
            return v.toMap().value(QStringLiteral("compatible")).toBool();
    return true;
}

bool RemoteCreateServer::dirOccupied(const QString &dir) const
{
    // 目录位于本地端机器；通过本地端 /api/fs/exists 判断（不支持或超时时视为未占用，允许继续）
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/api/fs/exists"), {{QStringLiteral("path"), dir}});
    QEventLoop loop;
    bool occupied = false;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(r, &QNetworkReply::finished, &loop, [&]() {
        if (r->error() == QNetworkReply::NoError)
            occupied = QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("exists")).toBool();
        r->deleteLater();
        loop.quit();
    });
    timer.start(3000);
    loop.exec();
    if (timer.isActive()) timer.stop();
    return occupied;
}

RemoteImportModpack::RemoteImportModpack(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() { if (m_rc->connected()) refresh(); });
}

void RemoteImportModpack::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/import"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            const QString nm = o.value(QStringLiteral("modpackName")).toString();
            if (nm != m_name) { m_name = nm; emit modpackNameChanged(); }
            const QString tp = o.value(QStringLiteral("detectedType")).toString();
            if (tp != m_type) { m_type = tp; emit detectedTypeChanged(); }
            const QString ld = o.value(QStringLiteral("loader")).toString();
            if (ld != m_loader) { m_loader = ld; emit loaderChanged(); }
            const QString gv = o.value(QStringLiteral("gameVersion")).toString();
            if (gv != m_ver) { m_ver = gv; emit gameVersionChanged(); }
            const QString st = o.value(QStringLiteral("statusText")).toString();
            if (st != m_status) { m_status = st; emit statusTextChanged(); }
            const bool busy = o.value(QStringLiteral("busy")).toBool();
            if (busy != m_busy) { m_busy = busy; emit busyChanged(); }
            const bool done = o.value(QStringLiteral("done")).toBool();
            if (done != m_done) { m_done = done; emit doneChanged(); }
            const qreal p = o.value(QStringLiteral("progress")).toDouble();
            if (qAbs(p - m_progress) > 1e-4) { m_progress = p; emit progressChanged(); }
        }
        r->deleteLater();
    });
}

void RemoteImportModpack::import()
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/import"),
        QJsonObject{{QStringLiteral("zipPath"), m_zip}, {QStringLiteral("targetDir"), m_dir}});
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}

void RemoteImportModpack::reset()
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/import/reset"), QJsonObject());
    connect(r, &QNetworkReply::finished, this, [this, r]() { r->deleteLater(); refresh(); });
}
