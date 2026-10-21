#pragma once

// ============================================================================
// Kernel / Execution / 性能监视（Performance Monitor）
// ----------------------------------------------------------------------------
// 采样整机 CPU / 内存占用，供侧边栏「设备占用」与 WebUI 展示。
// 原先内联在 main.cpp 中，随架构调整下沉为独立执行模块（头文件内联实现，无 .cpp）。
//
// 平台要点（踩过的坑）：
//   - Windows FILETIME 是 100ns 单位，与墙钟比较前必须换算，漏除会放大万倍（曾出现 7805%）；
//     这里用的是「两次采样的空闲/总时间增量之比」，不需要换算单位，天然规避该问题。
//   - Linux 必须单独实现 /proc/stat 与 /proc/meminfo（历史版本被 #ifdef Q_OS_WIN 包住，
//     导致 Linux 上恒为 0）。
// ============================================================================

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/sysinfo.h>   // 内存回退检测用（老内核无 MemAvailable 时）
#endif

class SystemMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY usageChanged)
    Q_PROPERTY(double memoryUsage READ memoryUsage NOTIFY usageChanged)
public:
    explicit SystemMonitor(QObject *parent = nullptr) : QObject(parent)
    {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SystemMonitor::update);
        m_timer->start(1500);
        update();
    }

    double cpuUsage() const { return m_cpu; }
    double memoryUsage() const { return m_mem; }

signals:
    void usageChanged();

private:
    void update()
    {
        double cpu = 0, mem = 0;
#ifdef Q_OS_WIN
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            auto toULL = [](FILETIME f) -> qint64 {
                ULARGE_INTEGER u; u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime;
                return qint64(u.QuadPart);
            };
            const qint64 idleT = toULL(idle);
            const qint64 sysT = toULL(kernel) + toULL(user);
            if (m_hasPrev) {
                const qint64 idleD = idleT - m_prevIdle;
                const qint64 sysD = sysT - m_prevSys;
                if (sysD > 0)
                    cpu = (1.0 - double(idleD) / double(sysD)) * 100.0;
            }
            m_prevIdle = idleT; m_prevSys = sysT; m_hasPrev = true;
        }
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
            mem = double(ms.dwMemoryLoad);
#else
        // Linux：读取 /proc/stat 与 /proc/meminfo，无需任何外部命令
        QFile fstat(QStringLiteral("/proc/stat"));
        if (fstat.open(QIODevice::ReadOnly)) {
            const QString line = QString::fromLatin1(fstat.readLine()).trimmed();
            fstat.close();
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                qint64 sum = 0;
                for (int i = 1; i < parts.size(); ++i) sum += parts.at(i).toLongLong();
                const qint64 idle = parts.at(4).toLongLong()
                                    + (parts.size() > 5 ? parts.at(5).toLongLong() : 0);
                if (m_hasPrev) {
                    const qint64 dTotal = sum - m_prevSys;
                    const qint64 dIdle  = idle - m_prevIdle;
                    if (dTotal > 0)
                        cpu = qBound(0.0, 100.0 * double(dTotal - dIdle) / double(dTotal), 100.0);
                }
                m_prevSys  = sum;
                m_prevIdle = idle;
                m_hasPrev  = true;
            }
        }
        {
            QFile fmem(QStringLiteral("/proc/meminfo"));
            qint64 total = 0, avail = 0, freeMem = 0;
            bool got = false;
            if (fmem.open(QIODevice::ReadOnly)) {
                while (!fmem.atEnd()) {
                    const QString l = QString::fromLatin1(fmem.readLine()).trimmed();
                    const QStringList kv = l.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (kv.size() >= 2) {
                        if (kv.at(0) == QStringLiteral("MemTotal:")) total = kv.at(1).toLongLong();
                        else if (kv.at(0) == QStringLiteral("MemAvailable:")) avail = kv.at(1).toLongLong();
                        else if (kv.at(0) == QStringLiteral("MemFree:")) freeMem = kv.at(1).toLongLong();
                    }
                }
                fmem.close();
                if (total > 0) {
                    const qint64 used = total - (avail > 0 ? avail : freeMem);
                    mem = qBound(0.0, 100.0 * double(used) / double(total), 100.0);
                    got = true;
                }
            }
            if (!got) {
                // 回退：sysinfo()（freeram 含页缓存，数值偏低但保证有值）
                struct sysinfo si;
                if (sysinfo(&si) == 0 && si.totalram > 0) {
                    const double used = double(si.totalram - si.freeram);
                    mem = qBound(0.0, 100.0 * used / double(si.totalram), 100.0);
                }
            }
        }
#endif
        if (qAbs(cpu - m_cpu) > 0.05 || qAbs(mem - m_mem) > 0.05) {
            m_cpu = cpu; m_mem = mem;
            emit usageChanged();
        }
    }

    double m_cpu = 0, m_mem = 0;
    bool m_hasPrev = false;
    qint64 m_prevIdle = 0, m_prevSys = 0;
    QTimer *m_timer = nullptr;
};
