#pragma once

// ============================================================================
// Kernel / Control / 本地直连（Local Direct Connection）
// ----------------------------------------------------------------------------
// 接口层访问内核的「本地」方式：同一进程内直接拿到服务对象，没有任何序列化与网络开销。
// 与 Control/Remote 下的三种远程连接（WebUI / 远控 / NapCat）并列，是 Control 层的第四种连接形态。
//
// 所有接口（Desktop GUI / CLI / TLI）都通过本类取用内核服务：
//   - GUI：bindTo() 把服务注册为 QML 上下文属性，QML 侧写法与重构前完全一致；
//   - CLI / TLI：service(name) 按名取用后调用其 Q_INVOKABLE。
// 这样「换一个前端」不需要重新接线，只需换一个接口层实现。
// ============================================================================

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QPointer>

#include "kernel.h"

class QQmlContext;

class LocalConnection : public QObject
{
    Q_OBJECT
public:
    explicit LocalConnection(Kernel *kernel, QObject *parent = nullptr);

    Kernel *kernel() const { return m_kernel.data(); }

    // 把内核服务注册为 QML 上下文属性。注册名与 ModuleBus 中的服务名一致。
    void bindTo(QQmlContext *ctx);

    // 非 QML 接口（CLI / TLI）按名取用内核服务
    Q_INVOKABLE QObject *service(const QString &name) const;
    Q_INVOKABLE bool hasService(const QString &name) const;
    Q_INVOKABLE QStringList serviceNames() const;
    // 诊断：名字 -> 类名
    Q_INVOKABLE QVariantMap servicesSnapshot() const;

private:
    QPointer<Kernel> m_kernel;
};
