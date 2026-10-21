// remoteproxymanager.h —— 远控端对桌面端 ProxyManager 的镜像
// 管理多个 RemoteProxyController 实例（与桌面端 ProxyManager 一一对应，索引 0 恒为默认实例），
// 供远控端 ProxyPage 的 Repeater 直接遍历（proxies 返回 QObject* 列表，与桌面端一致）。
#pragma once
#include <QObject>
#include <QList>
#include <QVariantList>
#include <QTimer>

class RemoteClient;
class RemoteProxyController;

class RemoteProxyManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList proxies READ proxies NOTIFY proxiesChanged)
    Q_PROPERTY(int count READ count NOTIFY proxiesChanged)
public:
    explicit RemoteProxyManager(RemoteClient *rc, QObject *parent = nullptr);
    QVariantList proxies() const;
    int count() const { return m_list.size(); }
    RemoteProxyController *defaultProxy() const { return m_list.value(0); }
    Q_INVOKABLE RemoteProxyController *proxyAt(int index) const;
    Q_INVOKABLE int addProxy(const QString &name);   // 异步：POST 后刷新，返回 -1（列表变化走 proxiesChanged）
    Q_INVOKABLE bool removeProxy(int index);         // 异步：删除指定实例（默认实例不可删由桌面端保证）
    void refresh();   // 拉取 /api/proxies 并同步实例列表

signals:
    void proxiesChanged();

private:
    RemoteProxyController *findOrCreate(const QString &id, const QString &name);
    RemoteClient *m_rc;
    QList<RemoteProxyController *> m_list;
    QTimer *m_timer = nullptr;
};
