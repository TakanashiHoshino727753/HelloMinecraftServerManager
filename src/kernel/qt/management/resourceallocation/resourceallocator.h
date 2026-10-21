#pragma once

// ============================================================================
// Kernel / Management / 系统资源分配（System Resource Allocation）
// ----------------------------------------------------------------------------
// 内核会同时抢占多种系统资源：并行下载（带宽/连接数）、多个 java 服务端（内存）、
// 代理与 WebUI（端口）。此前这些约束散落在各处：下载并行度由 DownloadManager 自持、
// 端口冲突检测由 ServerController / ProxyController 各写一份、内存上限只在 UI 里限制。
// 收敛到本类后，所有「能不能再开一个」的判定只有一个入口，策略可被 WebUI / CLI / 远控统一查询与调整。
// ============================================================================

#include <QObject>
#include <QString>
#include <QSet>

class ResourceAllocator : public QObject
{
    Q_OBJECT
    // 并行下载上限（0 = 不限制）
    Q_PROPERTY(int maxParallelDownloads READ maxParallelDownloads WRITE setMaxParallelDownloads
               NOTIFY limitsChanged)
    // 当前正在使用的下载槽位数
    Q_PROPERTY(int activeDownloads READ activeDownloads NOTIFY activeDownloadsChanged)
    // 单台服务器内存上限（MB），用于创建服务器向导与 WebUI 的默认值/上限约束
    Q_PROPERTY(int maxMemoryPerServerMb READ maxMemoryPerServerMb WRITE setMaxMemoryPerServerMb
               NOTIFY limitsChanged)
    Q_PROPERTY(int cpuCoreCount READ cpuCoreCount CONSTANT)
    Q_PROPERTY(qint64 totalMemoryMb READ totalMemoryMb CONSTANT)
public:
    explicit ResourceAllocator(QObject *parent = nullptr);

    // ---- 下载并发配额 ----
    int maxParallelDownloads() const { return m_maxParallelDownloads; }
    void setMaxParallelDownloads(int n);
    int activeDownloads() const { return m_activeDownloads; }

    // 尝试占用一个下载槽位；达到上限时返回 false（调用方应排队而不是直接发起下载）
    Q_INVOKABLE bool acquireDownloadSlot();
    Q_INVOKABLE void releaseDownloadSlot();

    // ---- 内存策略 ----
    int maxMemoryPerServerMb() const { return m_maxMemoryPerServerMb; }
    void setMaxMemoryPerServerMb(int mb);
    int cpuCoreCount() const;
    qint64 totalMemoryMb() const;

    // ---- 端口分配 ----
    // 端口是否被占用（尝试监听一次，0 表示随机端口探测）
    Q_INVOKABLE bool isPortFree(int port) const;
    // 预约端口：避免同一运行周期内两个模块抢同一个端口（不实际监听，仅登记）
    Q_INVOKABLE bool reservePort(int port);
    Q_INVOKABLE void releasePort(int port);
    Q_INVOKABLE bool isPortReserved(int port) const;

signals:
    void limitsChanged();
    void activeDownloadsChanged();

private:
    int m_maxParallelDownloads = 4;
    int m_activeDownloads = 0;
    int m_maxMemoryPerServerMb = 8192;
    QSet<int> m_reservedPorts;
    mutable int m_cachedCpuCores = 0;   // cpuCoreCount() 是 const，按需惰性缓存
};
