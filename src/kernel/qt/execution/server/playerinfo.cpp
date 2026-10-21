#include "playerinfo.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

QStringList PlayerInfo::acquireOffline(const QString &serverPath)
{
    QSet<QString> names;
    // whitelist.json / ops.json 形如 [{"name":"Steve"}, ...]
    for (const QString &file : { QStringLiteral("whitelist.json"), QStringLiteral("ops.json") }) {
        QFile f(serverPath + QLatin1Char('/') + file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isArray())
            continue;
        for (const QJsonValue &v : doc.array()) {
            if (v.isObject()) {
                const QString n = v.toObject().value(QStringLiteral("name")).toString();
                if (!n.isEmpty())
                    names.insert(n);
            }
        }
    }
    return organize(names.values());
}

QStringList PlayerInfo::organize(const QStringList &list)
{
    QSet<QString> s(list.begin(), list.end());
    QStringList out = s.values();
    out.sort(Qt::CaseInsensitive);
    return out;
}
