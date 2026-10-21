#include "commandtools.h"

QStringList CommandTools::instructionSet()
{
    // 服务端常用指令（Instruction Set）：覆盖权限、玩家管理、世界、规则、通信等高频操作
    return {
        QStringLiteral("op"), QStringLiteral("deop"),
        QStringLiteral("kick"), QStringLiteral("ban"), QStringLiteral("ban-ip"),
        QStringLiteral("pardon"), QStringLiteral("pardon-ip"),
        QStringLiteral("whitelist"), QStringLiteral("gamemode"), QStringLiteral("difficulty"),
        QStringLiteral("gamerule"), QStringLiteral("tp"), QStringLiteral("teleport"),
        QStringLiteral("give"), QStringLiteral("clear"), QStringLiteral("effect"),
        QStringLiteral("enchant"), QStringLiteral("summon"), QStringLiteral("kill"),
        QStringLiteral("time"), QStringLiteral("weather"), QStringLiteral("say"),
        QStringLiteral("tell"), QStringLiteral("msg"), QStringLiteral("me"),
        QStringLiteral("list"), QStringLiteral("stop"), QStringLiteral("reload"),
        QStringLiteral("setworldspawn"), QStringLiteral("spawnpoint"),
        QStringLiteral("fill"), QStringLiteral("setblock"), QStringLiteral("clone"),
        QStringLiteral("execute"), QStringLiteral("function"), QStringLiteral("tag"),
        QStringLiteral("team"), QStringLiteral("scoreboard"), QStringLiteral("data"),
        QStringLiteral("title"), QStringLiteral("playsound"), QStringLiteral("particle")
    };
}

QStringList CommandTools::complete(const QString &prefix)
{
    const QString p = prefix.trimmed().toLower();
    if (p.isEmpty())
        return instructionSet();
    QStringList out;
    for (const QString &c : instructionSet())
        if (c.startsWith(p, Qt::CaseInsensitive))
            out << c;
    return out;
}
