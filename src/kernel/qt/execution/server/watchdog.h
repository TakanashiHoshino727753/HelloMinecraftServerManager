#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>

class ServerControl;

// ============================================================================
// WatchDog（看门狗）：被动静默 + 主动心跳双检测，判定服务端卡死后强关并触发纠错
// ----------------------------------------------------------------------------
// 持有看门狗配置（开关/心跳周期/静默上限/心跳超时）与扫描定时器；
// 通过 ServerControl 暴露的访问器观察进程运行态、发送心跳、强制终止。
// 判定卡死时发出 triggered(path, reason, tail)，由外围记入异常纠错中心。
// ============================================================================
class WatchDog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int heartbeatSec READ heartbeatSec WRITE setHeartbeatSec NOTIFY heartbeatSecChanged)
    Q_PROPERTY(int silenceSec READ silenceSec WRITE setSilenceSec NOTIFY silenceSecChanged)
    Q_PROPERTY(int timeoutSec READ timeoutSec WRITE setTimeoutSec NOTIFY timeoutSecChanged)
public:
    explicit WatchDog(ServerControl *control, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool v);
    int heartbeatSec() const { return m_heartbeatSec; }
    void setHeartbeatSec(int v);
    int silenceSec() const { return m_silenceSec; }
    void setSilenceSec(int v);
    int timeoutSec() const { return m_timeoutSec; }
    void setTimeoutSec(int v);

    // 某服务器的看门狗健康状态（供 QML/WebUI 展示）
    Q_INVOKABLE QVariantMap status(const QString &path) const;

signals:
    void enabledChanged();
    void heartbeatSecChanged();
    void silenceSecChanged();
    void timeoutSecChanged();
    // 看门狗触发：服务器被判定卡死并强制终止，附带原因与尾部日志
    void triggered(const QString &path, const QString &reason, const QString &tail);

private:
    void tick();
    // 判定卡死后强制终止并触发纠错（不标记 intentionalKill，使 onFinished 视为异常）
    void trigger(const QString &path, const QString &reason);
    ServerControl *m_control;
    bool m_enabled = true;            // 总开关（默认开）
    int m_heartbeatSec = 60;          // 心跳指令周期（秒）
    int m_silenceSec = 300;           // 被动静默上限（秒）
    int m_timeoutSec = 20;            // 心跳发出后等待响应的超时（秒）
    QTimer *m_timer = nullptr;
};
