#include "resourceallocator.h"

#include <QTcpServer>
#include <QNetworkInterface>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

ResourceAllocator::ResourceAllocator(QObject *parent) : QObject(parent) {}

void ResourceAllocator::setMaxParallelDownloads(int n)
{
    const int v = n <= 0 ? 0 : n;
    if (m_maxParallelDownloads == v)
        return;
    m_maxParallelDownloads = v;
    emit limitsChanged();
}

bool ResourceAllocator::acquireDownloadSlot()
{
    if (m_maxParallelDownloads > 0 && m_activeDownloads >= m_maxParallelDownloads)
        return false;
    ++m_activeDownloads;
    emit activeDownloadsChanged();
    return true;
}

void ResourceAllocator::releaseDownloadSlot()
{
    if (m_activeDownloads <= 0)
        return;
    --m_activeDownloads;
    emit activeDownloadsChanged();
}

void ResourceAllocator::setMaxMemoryPerServerMb(int mb)
{
    const int v = mb <= 0 ? 0 : mb;
    if (m_maxMemoryPerServerMb == v)
        return;
    m_maxMemoryPerServerMb = v;
    emit limitsChanged();
}

int ResourceAllocator::cpuCoreCount() const
{
    if (m_cachedCpuCores > 0)
        return m_cachedCpuCores;
#ifdef Q_OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_cachedCpuCores = static_cast<int>(si.dwNumberOfProcessors);
#else
    m_cachedCpuCores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#endif
    if (m_cachedCpuCores < 1)
        m_cachedCpuCores = 1;
    return m_cachedCpuCores;
}

qint64 ResourceAllocator::totalMemoryMb() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return static_cast<qint64>(ms.ullTotalPhys / (1024 * 1024));
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || pageSize <= 0)
        return 0;
    return static_cast<qint64>(pages) * static_cast<qint64>(pageSize) / (1024 * 1024);
#endif
}

bool ResourceAllocator::isPortFree(int port) const
{
    if (port <= 0 || port > 65535)
        return false;
    if (m_reservedPorts.contains(port))
        return false;

    QTcpServer probe;
    // 双栈监听：任一协议栈能绑定即视为可用
    const bool ok = probe.listen(QHostAddress::Any, static_cast<quint16>(port))
                    || probe.listen(QHostAddress::AnyIPv6, static_cast<quint16>(port))
                    || probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port));
    if (ok)
        probe.close();
    return ok;
}

bool ResourceAllocator::reservePort(int port)
{
    if (port <= 0 || port > 65535)
        return false;
    if (m_reservedPorts.contains(port))
        return false;
    m_reservedPorts.insert(port);
    return true;
}

void ResourceAllocator::releasePort(int port)
{
    m_reservedPorts.remove(port);
}

bool ResourceAllocator::isPortReserved(int port) const
{
    return m_reservedPorts.contains(port);
}
