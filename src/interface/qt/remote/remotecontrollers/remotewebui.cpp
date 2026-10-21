#include "remotewebui.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

RemoteWebuiServer::RemoteWebuiServer(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteWebuiServer::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}

void RemoteWebuiServer::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/api/webui/status"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("enabled"), o.value(QStringLiteral("enabled")).toVariant());
            m_data.insert(QStringLiteral("running"), o.value(QStringLiteral("running")).toVariant());
            m_data.insert(QStringLiteral("paired"), o.value(QStringLiteral("paired")).toVariant());
            m_data.insert(QStringLiteral("mobileConnected"), o.value(QStringLiteral("mobileConnected")).toVariant());
            m_data.insert(QStringLiteral("remoteConnected"), o.value(QStringLiteral("remoteConnected")).toVariant());
            m_data.insert(QStringLiteral("remoteClients"), o.value(QStringLiteral("remoteClients")).toVariant());
            m_data.insert(QStringLiteral("port"), o.value(QStringLiteral("port")).toVariant());
            m_data.insert(QStringLiteral("https"), o.value(QStringLiteral("https")).toVariant());
            m_data.insert(QStringLiteral("error"), o.value(QStringLiteral("error")).toVariant());
            emit changed();
        }
        r->deleteLater();
    });
}

QString RemoteWebuiServer::pairUri() const
{
    return QStringLiteral("%1://localhost:%2/")
        .arg(https() ? QStringLiteral("https") : QStringLiteral("http"))
        .arg(port());
}

QString RemoteWebuiServer::generatePairCode()
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/api/webui/pair/generate"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("pairCode"), o.value(QStringLiteral("code")).toVariant());
            emit changed();
        }
        r->deleteLater();
    });
    return m_data.value(QStringLiteral("pairCode")).toString();
}

void RemoteWebuiServer::rebind()
{
    m_rc->apiPost(QStringLiteral("/api/webui/rebind"));
}
