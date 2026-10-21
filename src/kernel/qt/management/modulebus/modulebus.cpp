#include "modulebus.h"

ModuleBus::ModuleBus(QObject *parent) : QObject(parent) {}

void ModuleBus::registerService(const QString &name, QObject *service)
{
    if (name.isEmpty() || !service)
        return;
    m_services.insert(name, QPointer<QObject>(service));
    emit serviceRegistered(name);
}

void ModuleBus::unregisterService(const QString &name)
{
    m_services.remove(name);
}

bool ModuleBus::hasService(const QString &name) const
{
    return m_services.contains(name) && !m_services.value(name).isNull();
}

QObject *ModuleBus::service(const QString &name) const
{
    auto it = m_services.constFind(name);
    if (it == m_services.constEnd())
        return nullptr;
    return it.value().data();   // 对象已析构时 QPointer 返回 nullptr
}

QStringList ModuleBus::serviceNames() const
{
    QStringList names;
    for (auto it = m_services.constBegin(); it != m_services.constEnd(); ++it) {
        if (!it.value().isNull())
            names.append(it.key());
    }
    names.sort();
    return names;
}

QVariantMap ModuleBus::snapshot() const
{
    QVariantMap out;
    for (auto it = m_services.constBegin(); it != m_services.constEnd(); ++it) {
        QObject *obj = it.value().data();
        out.insert(it.key(), obj ? QString::fromLatin1(obj->metaObject()->className())
                                 : QStringLiteral("<destroyed>"));
    }
    return out;
}

void ModuleBus::subscribe(const QString &topic, QObject *receiver,
                          const std::function<void (const QVariant &)> &handler)
{
    if (topic.isEmpty() || !receiver || !handler)
        return;
    m_subs[topic].append(Subscription{QPointer<QObject>(receiver), handler});
}

void ModuleBus::publish(const QString &topic, const QVariant &payload)
{
    auto it = m_subs.constFind(topic);
    if (it == m_subs.constEnd())
        return;

    // 复制一份再遍历：处理函数内部可能再次 subscribe/unsubscribe，避免迭代器失效。
    QList<Subscription> subs = it.value();
    for (const Subscription &s : subs) {
        if (!s.receiver || !s.handler)
            continue;   // 订阅者已销毁
        s.handler(payload);
    }
    // 顺带清理该主题下已失效的订阅，避免长期运行后列表无限增长
    QList<Subscription> alive;
    for (const Subscription &s : subs) {
        if (s.receiver && s.handler)
            alive.append(s);
    }
    if (alive.isEmpty())
        m_subs.remove(topic);
    else
        m_subs.insert(topic, alive);

    emit eventPublished(topic, payload);
}
