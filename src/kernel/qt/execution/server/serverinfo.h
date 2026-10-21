#pragma once
#include <QString>
#include <QVariantMap>
#include <QPair>
#include <QProcess>

// ============================================================================
// Server Information Processing（服务端信息加工）
// ----------------------------------------------------------------------------
// 把「运行态采样」与「玩家名单整理」从 Server Control 中抽离：CPU/内存的
// 平台相关采样封装在此，Server Control 只负责循环与拼装 QVariantMap。
// ============================================================================

class ServerInfo
{
public:
    // 对单台运行中的进程采样 CPU/内存占用（平台相关）。
    // sample 为 "[cpuTimeMs, wallMs]" 的增量样本，调用方持有并传入以做差分；
    // 返回 { cpu(0~100*coreCount 等效单核%), memMB }。
    static QVariantMap sampleCpuMem(const QProcess *proc,
                                    QPair<qint64, qint64> &sample,
                                    int coreCount);
};
