#include "processconstruction.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

// 判断 JVM 参数字符串列表里是否已包含某前缀（避免重复追加 -D 参数）
static bool ulinesContains(const QStringList &lines, const QString &prefix)
{
    for (const QString &l : lines)
        if (l.startsWith(prefix))
            return true;
    return false;
}

QStringList ProcessConstruction::buildLaunchArgs(const QString &serverPath, int minMem, int maxMem,
                                                 bool *ok, QString *errMsg)
{
    QStringList args;
    const QString argsFile = findArgsFile(serverPath);
    const bool hasArgs = !argsFile.isEmpty();
    if (hasArgs) {
        // NeoForge / Forge 1.17+：把 UI 内存设置写入 user_jvm_args.txt（替换已有 -Xms/-Xmx 行），
        // 再 @引用参数文件（nogui 已在参数文件内，无需追加）。user_jvm_args.txt 若不存在则兜底创建。
        const QString ujvm = serverPath + QStringLiteral("/user_jvm_args.txt");
        QFile uf(ujvm);
        QStringList ulines;
        if (uf.open(QIODevice::ReadOnly)) {
            const QStringList old = QString::fromUtf8(uf.readAll())
                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &l : old)
                if (!l.contains(QLatin1String("-Xms")) && !l.contains(QLatin1String("-Xmx")))
                    ulines << l;
            uf.close();
        }
        ulines << QStringLiteral("-Xms%1M").arg(minMem) << QStringLiteral("-Xmx%1M").arg(maxMem);
        // NeoForge/Forge 1.17+ 的 args 文件里不带 nogui，主类会弹出 earlydisplay GUI 窗口；
        // 置 headless=true 抑制（日志可见 “Not loading early display in headless mode”）。
        if (!ulinesContains(ulines, QStringLiteral("-Djava.awt.headless")))
            ulines << QStringLiteral("-Djava.awt.headless=true");
        if (uf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            uf.write(ulines.join(QLatin1Char('\n')).toUtf8());
            uf.close();
        }
        args << QStringLiteral("@user_jvm_args.txt");
        args << (QStringLiteral("@") + argsFile);
    } else {
        // 探测实际启动 jar
        static const QStringList installerExclude = {
            QStringLiteral("*-installer*.jar"), QStringLiteral("installer*.jar.log") };
        QString launch;
        // Fabric 优先：fabric-server-launch.jar 才是真正的模组服启动器
        const QString fabricJar = findLaunchJar(serverPath, QStringLiteral("fabric-server-launch.jar"),
                                                installerExclude);
        if (!fabricJar.isEmpty()) {
            launch = fabricJar;
        } else {
            const QString forgeJar = findLaunchJar(serverPath, QStringLiteral("forge-"), installerExclude);
            if (!forgeJar.isEmpty()) {
                launch = forgeJar;
            } else {
                const QString neoJar = findLaunchJar(serverPath, QStringLiteral("neoforge-"), installerExclude);
                if (!neoJar.isEmpty())
                    launch = neoJar;
            }
        }
        if (launch.isEmpty() && QFile::exists(serverPath + QStringLiteral("/server.jar")))
            launch = QDir(serverPath).absoluteFilePath(QStringLiteral("server.jar"));
        if (launch.isEmpty()) {
            if (ok) *ok = false;
            if (errMsg)
                *errMsg = QStringLiteral("未找到任何可识别的服务端核心"
                    "（server.jar / fabric-server-launch.jar / forge-*.jar / neoforge-*.jar / args 文件）。"
                    "请重新创建服务器。");
            return {};
        }
        args << QStringLiteral("-Xms%1M").arg(minMem)
             << QStringLiteral("-Xmx%1M").arg(maxMem)
             << QStringLiteral("-jar") << launch
             << QStringLiteral("nogui");
    }
    if (ok) *ok = true;
    return args;
}

QString ProcessConstruction::findLaunchJar(const QString &dir, const QString &prefix,
                                           const QStringList &exclude)
{
    QDir d(dir);
    if (!d.exists())
        return QString();
    const QFileInfoList files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : files) {
        const QString name = fi.fileName();
        bool skipped = false;
        for (const QString &ex : exclude) {
            // ex 形如 "*-installer*.jar"，转成正则：*→[^/]*，?→.
            const QString pattern = QRegularExpression::anchoredPattern(
                QRegularExpression::wildcardToRegularExpression(ex));
            QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
            if (re.match(name).hasMatch()) { skipped = true; break; }
        }
        if (skipped)
            continue;
        if (name.startsWith(prefix, Qt::CaseInsensitive))
            return fi.absoluteFilePath();
    }
    return QString();
}

QString ProcessConstruction::findArgsFile(const QString &dir)
{
    const QStringList names = { QStringLiteral("win_args.txt"), QStringLiteral("unix_args.txt") };
#ifdef Q_OS_WIN
    const QString wanted = QStringLiteral("win_args.txt");
#else
    const QString wanted = QStringLiteral("unix_args.txt");
#endif
    QDir d(dir);
    if (d.exists()) {
        // 1) 根目录优先
        for (const QString &n : names)
            if (QFile::exists(dir + QStringLiteral("/") + n))
                return n;
    }
    // 2) libraries/ 递归扫描（NeoForge 实际产物位置）
    const QString libDir = dir + QStringLiteral("/libraries");
    if (QFileInfo::exists(libDir)) {
        QDirIterator it(libDir, QStringList() << QStringLiteral("*.txt"),
                        QDir::Files, QDirIterator::Subdirectories);
        QStringList found;
        while (it.hasNext()) {
            const QString abs = it.next();
            const QString base = QFileInfo(abs).fileName();
            if (names.contains(base, Qt::CaseInsensitive))
                found << d.relativeFilePath(abs);
        }
        if (!found.isEmpty()) {
            for (const QString &f : found)
                if (QFileInfo(f).fileName().compare(wanted, Qt::CaseInsensitive) == 0)
                    return f;
            return found.first();
        }
    }
    return QString();
}
