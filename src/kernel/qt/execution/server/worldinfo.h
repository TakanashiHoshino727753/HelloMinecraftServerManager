#pragma once
#include <QString>
#include <QStringList>

// ============================================================================
// World（世界）：Information Acquisition + Information Organization + Backup/Restore
// ----------------------------------------------------------------------------
// 世界目录列举、体积统计，以及世界备份与恢复（目录级复制）。
// ============================================================================

class WorldInfo
{
public:
    // 信息获取：列出服务器下的世界目录
    // （world / world_nether / world_the_end 及其 world_* 变种）
    static QStringList listWorlds(const QString &serverPath);
    // 信息组织：单个世界目录体积（字节）
    static qint64 worldSize(const QString &serverPath, const QString &worldName);
    // 备份与恢复：将世界目录复制为 destDir（已存在则覆盖）；返回是否成功
    static bool backupWorld(const QString &serverPath, const QString &worldName,
                            const QString &destDir);
    static bool restoreWorld(const QString &serverPath, const QString &worldName,
                             const QString &srcDir);
};
