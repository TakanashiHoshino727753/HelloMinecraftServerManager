// remotesystem.cpp —— 见 remotesystem.h
#include "remotesystem.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>

RemoteSystemMonitor::RemoteSystemMonitor(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, &RemoteSystemMonitor::update);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { update(); m_timer->start(); }
        else { m_timer->stop(); }
    });
}

void RemoteSystemMonitor::update()
{
    QNetworkReply *r = m_rc->apiGet("/api/system");
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            const double cpu = o.value("cpu").toDouble();
            const double mem = o.value("mem").toDouble();
            if (qAbs(cpu - m_cpu) > 0.05 || qAbs(mem - m_mem) > 0.05) {
                m_cpu = cpu;
                m_mem = mem;
                emit usageChanged();
            }
        }
        r->deleteLater();
    });
}
