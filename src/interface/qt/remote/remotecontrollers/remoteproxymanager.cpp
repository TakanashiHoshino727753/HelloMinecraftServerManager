// remoteproxymanager.cpp —— 见 remoteproxymanager.h
#include "remoteproxymanager.h"
#include "remoteproxycontroller.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>

RemoteProxyManager::RemoteProxyManager(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteProxyManager::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); }
        else m_timer->stop();
    });
}

QVariantList RemoteProxyManager::proxies() const
{
    QVariantList r;
    for (RemoteProxyController *c : m_list)
        r.append(QVariant::fromValue(static_cast<QObject *>(c)));
    return r;
}

RemoteProxyController *RemoteProxyManager::proxyAt(int index) const
{
    return m_list.value(index);
}

RemoteProxyController *RemoteProxyManager::findOrCreate(const QString &id, const QString &name)
{
    for (RemoteProxyController *c : m_list) {
        if (c->instanceId() == id)
            return c;
    }
    auto *c = new RemoteProxyController(m_rc, id, name, this);
    m_list.append(c);
    return c;
}

int RemoteProxyManager::addProxy(const QString &name)
{
    QNetworkReply *r = m_rc->apiPost(QStringLiteral("/api/proxies"), QJsonObject{{QStringLiteral("name"), name}});
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        refresh();
    });
    return -1;
}

bool RemoteProxyManager::removeProxy(int index)
{
    RemoteProxyController *c = m_list.value(index);
    if (!c) return false;
    QNetworkReply *r = m_rc->apiPost("/api/proxies/" + c->instanceId() + QStringLiteral("/delete"), QJsonObject());
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        refresh();
    });
    return true;
}

void RemoteProxyManager::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/api/proxies"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() != QNetworkReply::NoError) { r->deleteLater(); return; }
        const QJsonArray arr = QJsonDocument::fromJson(r->readAll())
                                   .object().value(QStringLiteral("proxies")).toArray();
        r->deleteLater();

        QSet<QString> seen;
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("instanceId")).toString();
            const QString name = o.value(QStringLiteral("name")).toString();
            RemoteProxyController *c = findOrCreate(id, name);
            // 摘要里也带 running/type/servers，顺手更新
            c->refresh();
            seen.insert(id);
        }
        // 移除桌面端已删除的实例
        bool changed = false;
        for (int i = m_list.size() - 1; i >= 0; --i) {
            if (!seen.contains(m_list.at(i)->instanceId())) {
                delete m_list.takeAt(i);
                changed = true;
            }
        }
        if (changed) emit proxiesChanged();
    });
}
