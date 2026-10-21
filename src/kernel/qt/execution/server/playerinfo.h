#pragma once
#include <QString>
#include <QStringList>

// ============================================================================
// Player（玩家）：Information Acquisition + Information Organization
// ----------------------------------------------------------------------------
// 离线信息获取：从 whitelist.json / ops.json 读取受管玩家名（不依赖进程运行）；
// 信息组织：去重排序，供界面统一展示。
// （运行中的实时在线名单仍由 Server Control 的进程输出解析得到。）
// ============================================================================

class PlayerInfo
{
public:
    // 信息获取：从 whitelist.json / ops.json 离线读取受管玩家名
    static QStringList acquireOffline(const QString &serverPath);
    // 信息组织：去重并排序
    static QStringList organize(const QStringList &list);
    static int count(const QStringList &list) { return list.size(); }
};
