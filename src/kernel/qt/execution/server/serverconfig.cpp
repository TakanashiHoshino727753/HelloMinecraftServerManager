#include "serverconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTextStream>

namespace {
QVariantMap parseProperties(QTextStream &ts)
{
    QVariantMap map;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int idx = line.indexOf(QLatin1Char('='));
        if (idx <= 0)
            continue;
        map.insert(line.left(idx).trimmed(), line.mid(idx + 1).trimmed());
    }
    return map;
}

void writeDefaultProperties(const QString &file)
{
    QDir().mkpath(QFileInfo(file).path());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    const QStringList defaults = {
        QStringLiteral("#Minecraft server properties (由 HMCSM 生成的默认配置)"),
        QStringLiteral("allow-flight=false"),
        QStringLiteral("allow-nether=true"),
        QStringLiteral("difficulty=easy"),
        QStringLiteral("enable-command-block=false"),
        QStringLiteral("enable-rcon=false"),
        QStringLiteral("force-gamemode=false"),
        QStringLiteral("gamemode=survival"),
        QStringLiteral("generate-structures=true"),
        QStringLiteral("level-name=world"),
        QStringLiteral("level-seed="),
        QStringLiteral("level-type=minecraft:normal"),
        QStringLiteral("max-build-height=256"),
        QStringLiteral("max-players=20"),
        QStringLiteral("max-world-size=29999984"),
        QStringLiteral("motd=A Minecraft Server"),
        QStringLiteral("online-mode=true"),
        QStringLiteral("op-permission-level=4"),
        QStringLiteral("player-idle-timeout=0"),
        QStringLiteral("pvp=true"),
        QStringLiteral("server-ip="),
        QStringLiteral("server-port=25565"),
        QStringLiteral("spawn-animals=true"),
        QStringLiteral("spawn-monsters=true"),
        QStringLiteral("spawn-protection=16"),
        QStringLiteral("view-distance=10"),
        QStringLiteral("white-list=false")
    };
    for (const QString &l : defaults)
        ts << l << QLatin1Char('\n');
}
}

ServerConfig::ServerConfig(QObject *parent) : QObject(parent) {}

QVariantMap ServerConfig::readProperties(const QString &serverPath)
{
    if (serverPath.isEmpty())
        return {};
    const QString file = serverPath + QStringLiteral("/server.properties");
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 文件不存在：写入默认 server.properties，便于查看与编辑
        writeDefaultProperties(file);
        QFile f2(file);
        if (f2.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f2);
            ts.setEncoding(QStringConverter::Utf8);
            return parseProperties(ts);
        }
        return {};
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    return parseProperties(ts);
}

void ServerConfig::writeProperties(const QString &serverPath, const QVariantMap &map)
{
    const QString file = serverPath + QStringLiteral("/server.properties");
    QStringList outLines;
    bool changed = false;

    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        while (!ts.atEnd()) {
            QString line = ts.readLine();
            if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#'))) {
                outLines << line;
                continue;
            }
            const int idx = line.indexOf(QLatin1Char('='));
            if (idx <= 0) { outLines << line; continue; }
            const QString key = line.left(idx).trimmed();
            if (map.contains(key)) {
                const QString val = map.value(key).toString();
                outLines << (key + QStringLiteral("=") + val);
                changed = true;
            } else {
                outLines << line;
            }
        }
        f.close();
    }

    // 追加文件中不存在的键
    for (auto it = map.begin(); it != map.end(); ++it) {
        bool exists = false;
        for (const QString &l : outLines) {
            if (l.startsWith(it.key() + QLatin1Char('=')) || l.startsWith(it.key() + QStringLiteral(" ="))) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            outLines << (it.key() + QStringLiteral("=") + it.value().toString());
            changed = true;
        }
    }

    if (!changed)
        return;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[ServerConfig] 无法写入" << file;
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString &l : outLines)
        out << l << QLatin1Char('\n');
    f.close();
}

int ServerConfig::serverPort(const QString &serverPath) const
{
    QFile f(serverPath + QStringLiteral("/server.properties"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QStringLiteral("server-port="))) {
                bool ok = false;
                const int p = line.mid(12).trimmed().toInt(&ok);
                if (ok && p > 0 && p < 65536)
                    return p;
            }
        }
    }
    return 25565;
}

bool ServerConfig::isPortFree(int port) const
{
    if (port <= 0 || port >= 65536)
        return false;
    QTcpServer probe;
    const bool ok = probe.listen(QHostAddress::AnyIPv4, quint16(port));
    probe.close();
    return ok;
}

int ServerConfig::assignFreePort(const QString &serverPath, const QSet<int> &used)
{
    const int base = serverPort(serverPath);
    for (int p = base + 1; p < base + 500 && p < 65536; ++p) {
        if (used.contains(p) || !isPortFree(p))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("server-port"), QString::number(p));
        writeProperties(serverPath, m);
        return p;
    }
    return -1;
}
