#pragma once
#include <QObject>
#include <QStringList>

// ============================================================================
// Mods（模组）：Support Detection + Mods List Retrieval + Conflict Exclusion
// ----------------------------------------------------------------------------
// 是否为模组服（mods/ 目录存在）、列举 mods/ 下的文件、以及简单的冲突排除
// （同名/大小写不同的重复文件视为潜在冲突）。
// ============================================================================

class ModsManager : public QObject
{
    Q_OBJECT
public:
    explicit ModsManager(QObject *parent = nullptr);

    // 支持检测：该服务端是否含 mods 目录（即是否为模组服）
    Q_INVOKABLE bool isModded(const QString &serverPath) const;
    // 模组列表获取：mods/ 下所有文件名
    Q_INVOKABLE QStringList listMods(const QString &serverPath) const;
    // 冲突排除：返回疑似冲突（重复/同名不同大小写）的模组文件名列表
    Q_INVOKABLE QStringList conflictExclusion(const QString &serverPath) const;
};
