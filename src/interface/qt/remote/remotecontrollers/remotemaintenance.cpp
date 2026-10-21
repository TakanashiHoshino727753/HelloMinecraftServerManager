#include "remotemaintenance.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>

static void applyJson(QVariantMap &m, const QJsonObject &o, bool &changed)
{
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (m.value(it.key()) != it.value().toVariant()) { m.insert(it.key(), it.value().toVariant()); changed = true; }
}

RemotePortMapper::RemotePortMapper(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemotePortMapper::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}

void RemotePortMapper::apply(const QJsonObject &o)
{
    bool c = false; applyJson(m_data, o, c);
    if (c) emit dataChanged();
}

void RemotePortMapper::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/portmap"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) apply(QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    });
}
void RemotePortMapper::discover() { m_rc->apiGet(QStringLiteral("/portmap/discover")); refresh(); }
void RemotePortMapper::addMapping(int ext, int in, const QString &proto, const QString &desc)
{
    m_rc->apiGet(QStringLiteral("/portmap/add"), {{"ext", ext}, {"int", in}, {"proto", proto}, {"desc", desc}}); refresh();
}
void RemotePortMapper::removeMapping(int ext, const QString &proto)
{
    m_rc->apiGet(QStringLiteral("/portmap/remove"), {{"ext", ext}, {"proto", proto}}); refresh();
}
void RemotePortMapper::refreshExternalIp() { m_rc->apiGet(QStringLiteral("/portmap/external")); refresh(); }

RemoteBackupController::RemoteBackupController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteBackupController::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}
void RemoteBackupController::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/backup"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) { bool c = false; applyJson(m_data, QJsonDocument::fromJson(r->readAll()).object(), c); if (c) emit dataChanged(); }
        r->deleteLater();
    });
}
void RemoteBackupController::set(const QString &field, const QVariant &value)
{
    m_rc->apiPut(QStringLiteral("/backup"), QJsonObject{{field, QJsonValue::fromVariant(value)}});
    m_data.insert(field, value); emit dataChanged();
}
void RemoteBackupController::backupNow(const QString &name, const QString &path)
{
    m_rc->apiPost(QStringLiteral("/backup/now"), QJsonObject{{"name", name}, {"path", path}}); refresh();
}

RemoteSchedulerController::RemoteSchedulerController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteSchedulerController::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}
void RemoteSchedulerController::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/scheduler"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            QVariantList tasks = QJsonDocument::fromJson(r->readAll()).object().value("tasks").toArray().toVariantList();
            if (m_data.value("tasks").toList() != tasks) { m_data.insert("tasks", tasks); emit dataChanged(); }
        }
        r->deleteLater();
    });
}
void RemoteSchedulerController::addTask(const QVariantMap &task)
{
    m_rc->apiPost(QStringLiteral("/scheduler"), QJsonObject{{"task", QJsonObject::fromVariantMap(task)}}); refresh();
}
void RemoteSchedulerController::updateTask(int index, const QVariantMap &task)
{
    m_rc->apiPut(QStringLiteral("/scheduler"), QJsonObject{{"index", index}, {"task", QJsonObject::fromVariantMap(task)}}); refresh();
}
void RemoteSchedulerController::removeTask(int index)
{
    m_rc->apiDelete(QStringLiteral("/scheduler") + QStringLiteral("?index=") + QString::number(index)); refresh();
}
void RemoteSchedulerController::runTask(int index)
{
    m_rc->apiPost(QStringLiteral("/scheduler/run"), QJsonObject{{"index", index}}); refresh();
}

RemoteUpdateController::RemoteUpdateController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteUpdateController::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}
void RemoteUpdateController::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/update"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) { bool c = false; applyJson(m_data, QJsonDocument::fromJson(r->readAll()).object().value("info").toObject(), c); if (c) emit dataChanged(); }
        r->deleteLater();
    });
}
void RemoteUpdateController::checkUpdate(const QString &name, const QString &type, const QString &version)
{
    m_rc->apiPost(QStringLiteral("/update/check"), QJsonObject{{"name", name}, {"type", type}, {"version", version}}); refresh();
}
void RemoteUpdateController::updateJar(const QString &name, const QString &path, const QString &type, const QString &version)
{
    m_rc->apiPost(QStringLiteral("/update/do"), QJsonObject{{"name", name}, {"path", path}, {"type", type}, {"version", version}}); refresh();
}
