#include "modsmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>

ModsManager::ModsManager(QObject *parent) : QObject(parent) {}

bool ModsManager::isModded(const QString &serverPath) const
{
    return QFileInfo::exists(serverPath + QStringLiteral("/mods"));
}

QStringList ModsManager::listMods(const QString &serverPath) const
{
    QDir dir(serverPath + QStringLiteral("/mods"));
    if (!dir.exists())
        return {};
    QStringList names;
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
        names << fi.fileName();
    return names;
}

QStringList ModsManager::conflictExclusion(const QString &serverPath) const
{
    const QStringList all = listMods(serverPath);
    // 以「小写文件名」为键聚合：同一键出现多次（或文件名相同仅大小写不同）视为潜在冲突
    QMap<QString, QStringList> groups;  // 小写名 -> 原始名列表
    for (const QString &n : all)
        groups[n.toLower()].append(n);
    QStringList conflicts;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (it.value().size() > 1)
            conflicts.append(it.value());
    }
    return conflicts;
}
