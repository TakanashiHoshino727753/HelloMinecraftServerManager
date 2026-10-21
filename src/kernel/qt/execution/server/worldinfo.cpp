#include "worldinfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

QStringList WorldInfo::listWorlds(const QString &serverPath)
{
    QStringList out;
    QDir dir(serverPath);
    if (!dir.exists())
        return out;
    // 标准三世界
    const QStringList standard = {
        QStringLiteral("world"), QStringLiteral("world_nether"), QStringLiteral("world_the_end") };
    for (const QString &w : standard)
        if (QFileInfo::exists(serverPath + QLatin1Char('/') + w))
            out << w;
    // 其余 world_* 变种（如插件生成的多世界）
    const QFileInfoList sub = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : sub) {
        const QString n = fi.fileName();
        if (n.startsWith(QStringLiteral("world")) && !out.contains(n))
            out << n;
    }
    return out;
}

qint64 WorldInfo::worldSize(const QString &serverPath, const QString &worldName)
{
    const QString path = serverPath + QLatin1Char('/') + worldName;
    QDir dir(path);
    if (!dir.exists())
        return 0;
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// 递归复制目录（src -> dst，dst 已存在则清空后复制）
static bool copyDirRecursive(const QString &src, const QString &dst)
{
    QDir srcDir(src);
    if (!srcDir.exists())
        return false;
    QDir dstDir;
    if (dstDir.exists(dst))
        dstDir.removeRecursively();
    if (!dstDir.mkpath(dst))
        return false;
    QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString target = dst + QLatin1Char('/') + srcDir.relativeFilePath(fi.filePath());
        if (fi.isDir()) {
            if (!dstDir.mkpath(target))
                return false;
        } else {
            QDir().mkpath(QFileInfo(target).path());
            if (!QFile::copy(fi.filePath(), target))
                return false;
        }
    }
    return true;
}

bool WorldInfo::backupWorld(const QString &serverPath, const QString &worldName, const QString &destDir)
{
    return copyDirRecursive(serverPath + QLatin1Char('/') + worldName, destDir);
}

bool WorldInfo::restoreWorld(const QString &serverPath, const QString &worldName, const QString &srcDir)
{
    return copyDirRecursive(srcDir, serverPath + QLatin1Char('/') + worldName);
}
