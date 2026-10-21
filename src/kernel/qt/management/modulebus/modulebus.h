#pragma once

// ============================================================================
// Kernel / Management / 模块间调用（Inter-Module Invocation）
// ----------------------------------------------------------------------------
// ModuleBus 是内核内部各执行模块互相发现的唯一通道：
//   - 服务注册表：模块以名字注册自己，调用方按名字解析（避免彼此直接 #include 与持有裸指针，
//     从而消除 java <-> download 这类双向依赖导致的层次混乱）。
//   - 事件总线：以「主题 + 负载」广播，模块之间用事件而非直接信号连线，新增模块无需改动既有模块。
//
// 约定：注册名统一使用小写驼峰（如 "serverManager"），与 QML 上下文属性名保持一致，
// 这样本地直连（LocalConnection）把服务暴露给接口层时，两套名字是同一份真相。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QMap>
#include <QPointer>
#include <functional>

class ModuleBus : public QObject
{
    Q_OBJECT
public:
    explicit ModuleBus(QObject *parent = nullptr);

    // ---- 服务注册表 ----
    // 同名重复注册会覆盖旧值并发出 serviceRegistered（后注册者胜出，便于测试替换实现）。
    void registerService(const QString &name, QObject *service);
    void unregisterService(const QString &name);
    Q_INVOKABLE bool hasService(const QString &name) const;
    Q_INVOKABLE QObject *service(const QString &name) const;
    Q_INVOKABLE QStringList serviceNames() const;

    // 类型安全取用：名字错误或类型不符时返回 nullptr（不抛异常、不做 qobject_cast 断言）
    template <class T>
    T *serviceAs(const QString &name) const
    {
        return qobject_cast<T *>(service(name));
    }

    // 调试/诊断用：名字 -> 类名 快照
    Q_INVOKABLE QVariantMap snapshot() const;

    // ---- 事件总线 ----
    // 同步广播（DirectConnection 语义）。订阅者对象析构后自动退订（QPointer 守卫）。
    void subscribe(const QString &topic, QObject *receiver,
                   const std::function<void (const QVariant &)> &handler);
    Q_INVOKABLE void publish(const QString &topic, const QVariant &payload = QVariant());
    Q_INVOKABLE void publish(const QString &topic, const QString &text)
    {
        publish(topic, QVariant(text));
    }

signals:
    void serviceRegistered(const QString &name);
    void eventPublished(const QString &topic, const QVariant &payload);

private:
    struct Subscription {
        QPointer<QObject> receiver;
        std::function<void (const QVariant &)> handler;
    };

    QMap<QString, QPointer<QObject>> m_services;
    QMap<QString, QList<Subscription>> m_subs;
};
