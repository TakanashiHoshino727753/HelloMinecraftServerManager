// remotesystem.h —— 远控端对桌面端 SystemMonitor 的镜像
// 桌面端直接读本机 CPU/内存；远控端改为轮询本地端 WebUI 的 /api/system。
#pragma once
#include <QObject>
#include <QTimer>

class RemoteClient;

class RemoteSystemMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY usageChanged)
    Q_PROPERTY(double memoryUsage READ memoryUsage NOTIFY usageChanged)
public:
    explicit RemoteSystemMonitor(RemoteClient *rc, QObject *parent = nullptr);
    double cpuUsage() const { return m_cpu; }
    double memoryUsage() const { return m_mem; }

signals:
    void usageChanged();

private:
    void update();
    RemoteClient *m_rc;
    double m_cpu = 0, m_mem = 0;
    QTimer *m_timer = nullptr;
};
