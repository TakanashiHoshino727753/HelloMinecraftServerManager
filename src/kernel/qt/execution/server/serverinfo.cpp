#include "serverinfo.h"

#include <QDateTime>
#include <QThread>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>   // sysconf(_SC_CLK_TCK)，用于 /proc 时钟滴答换算
#endif

QVariantMap ServerInfo::sampleCpuMem(const QProcess *proc,
                                     QPair<qint64, qint64> &sample,
                                     int coreCount)
{
    QVariantMap m;
    double cpu = 0;
    qint64 memBytes = 0;
    const int pid = proc->processId();
    if (pid > 0) {
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
        if (h) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                memBytes = pmc.WorkingSetSize;
            FILETIME c, e, k, u;
            if (GetProcessTimes(h, &c, &e, &k, &u)) {
                auto toQ = [](FILETIME f) -> qint64 {
                    ULARGE_INTEGER i;
                    i.LowPart = f.dwLowDateTime;
                    i.HighPart = f.dwHighDateTime;
                    return qint64(i.QuadPart);
                };
                // FILETIME 单位为 100 纳秒，转为毫秒与 wall 量纲统一，否则 CPU% 会放大 1e4 倍
                const qint64 cpuTimeMs = (toQ(k) + toQ(u)) / 10000;
                const qint64 wallMs = QDateTime::currentMSecsSinceEpoch();
                if (sample.second > 0) {
                    const qint64 dCpu = cpuTimeMs - sample.first;
                    const qint64 dWall = wallMs - sample.second;
                    if (dWall > 0)
                        cpu = double(dCpu) / double(dWall) * 100.0 / coreCount;
                }
                sample.first = cpuTimeMs;
                sample.second = wallMs;
            }
            CloseHandle(h);
        }
#else
        const QString procDir = QStringLiteral("/proc/%1").arg(pid);
        QFile statFile(procDir + QStringLiteral("/stat"));
        if (statFile.open(QIODevice::ReadOnly)) {
            const QByteArray data = statFile.readAll();
            statFile.close();
            // comm 可能含空格/括号，从最后一个 ')' 之后解析剩余字段
            const int rp = data.lastIndexOf(')');
            if (rp > 0) {
                const QList<QByteArray> f = data.mid(rp + 1).trimmed().split(' ');
                // 1-based: 14=utime, 15=stime -> 相对 ')' 后的索引为 13, 14
                if (f.size() >= 15) {
                    const qint64 ticks = f[13].toLongLong() + f[14].toLongLong();
                    const long clk = sysconf(_SC_CLK_TCK);
                    const qint64 cpuTimeMs = (clk > 0) ? (ticks * 1000 / clk) : 0;
                    const qint64 wallMs = QDateTime::currentMSecsSinceEpoch();
                    if (sample.second > 0) {
                        const qint64 dCpu = cpuTimeMs - sample.first;
                        const qint64 dWall = wallMs - sample.second;
                        if (dWall > 0)
                            cpu = double(dCpu) / double(dWall) * 100.0 / coreCount;
                    }
                    sample.first = cpuTimeMs;
                    sample.second = wallMs;
                }
            }
        }
        QFile statusFile(procDir + QStringLiteral("/status"));
        if (statusFile.open(QIODevice::ReadOnly)) {
            while (!statusFile.atEnd()) {
                const QByteArray line = statusFile.readLine();
                if (line.startsWith("VmRSS:")) {
                    const QList<QByteArray> p = line.split(' ');
                    for (const QByteArray &tok : p) {
                        bool ok = false;
                        const qint64 kb = tok.toLongLong(&ok);
                        if (ok) { memBytes = kb * 1024; break; }
                    }
                    break;
                }
            }
            statusFile.close();
        }
#endif
    }
    m[QStringLiteral("cpu")] = cpu;
    m[QStringLiteral("memMB")] = memBytes / (1024.0 * 1024.0);
    return m;
}
