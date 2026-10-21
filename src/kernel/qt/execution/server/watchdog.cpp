#include "watchdog.h"
#include "servercontrol.h"

#include <QDateTime>
#include <QSettings>

WatchDog::WatchDog(ServerControl *control, QObject *parent)
    : QObject(parent), m_control(control)
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("server/watchdogEnabled"), true).toBool();
    m_heartbeatSec = s.value(QStringLiteral("server/watchdogHeartbeatSec"), 60).toInt();
    m_silenceSec = s.value(QStringLiteral("server/watchdogSilenceSec"), 300).toInt();
    m_timeoutSec = s.value(QStringLiteral("server/watchdogTimeoutSec"), 20).toInt();
    // 每 5 秒扫描一次所有运行进程（比最短超时都短，保证及时性且不浪费 CPU）
    m_timer = new QTimer(this);
    m_timer->setInterval(5000);
    connect(m_timer, &QTimer::timeout, this, &WatchDog::tick);
    m_timer->start();
}

void WatchDog::setEnabled(bool v)
{
    if (v == m_enabled)
        return;
    m_enabled = v;
    QSettings().setValue(QStringLiteral("server/watchdogEnabled"), v);
    emit enabledChanged();
}

void WatchDog::setHeartbeatSec(int v)
{
    if (v == m_heartbeatSec)
        return;
    m_heartbeatSec = qMax(5, v);
    QSettings().setValue(QStringLiteral("server/watchdogHeartbeatSec"), m_heartbeatSec);
    emit heartbeatSecChanged();
}

void WatchDog::setSilenceSec(int v)
{
    if (v == m_silenceSec)
        return;
    m_silenceSec = qMax(10, v);
    QSettings().setValue(QStringLiteral("server/watchdogSilenceSec"), m_silenceSec);
    emit silenceSecChanged();
}

void WatchDog::setTimeoutSec(int v)
{
    if (v == m_timeoutSec)
        return;
    m_timeoutSec = qMax(3, v);
    QSettings().setValue(QStringLiteral("server/watchdogTimeoutSec"), m_timeoutSec);
    emit timeoutSecChanged();
}

QVariantMap WatchDog::status(const QString &path) const
{
    QVariantMap m;
    m[QStringLiteral("enabled")] = m_enabled;
    const qint64 lastStdout = m_control->procLastStdoutMs(path);
    if (lastStdout == 0) {
        m[QStringLiteral("running")] = false;
        return m;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m[QStringLiteral("running")] = true;
    m[QStringLiteral("lastStdoutSec")] = int((now - lastStdout) / 1000);
    const qint64 lastHb = m_control->procLastHeartbeatMs(path);
    m[QStringLiteral("lastHeartbeatSec")] = lastHb ? int((now - lastHb) / 1000) : -1;
    m[QStringLiteral("pendingHeartbeat")] = m_control->procHeartbeatPending(path);
    m[QStringLiteral("healthy")] = (now - lastStdout) / 1000 < m_silenceSec;
    return m;
}

void WatchDog::tick()
{
    if (!m_enabled || m_control->runningCount() == 0)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // runningServerNames() 返回即身份键 path（与 ServerControl 内部一致）；
    // 自此逐台做双检测：被动静默 / 主动心跳。
    for (const QString &path : m_control->runningServerNames()) {
        const qint64 sinceStdout = (now - m_control->procLastStdoutMs(path)) / 1000;

        // 1) 被动静默超时：长时间无任何输出（世界可能卡死/IO 阻塞）
        if (sinceStdout >= m_silenceSec) {
            trigger(path, QStringLiteral("io"));
            continue;
        }

        // 2) 主动心跳：到点则发一次 list（list 在所有主流服务端都会回显在线人数）
        const qint64 sinceHeartbeat = m_control->procLastHeartbeatMs(path)
                ? (now - m_control->procLastHeartbeatMs(path)) / 1000
                : (now - m_control->procLastStdoutMs(path)) / 1000;
        if (!m_control->procHeartbeatPending(path) && sinceHeartbeat >= m_heartbeatSec) {
            m_control->sendListHeartbeat(path);
            continue;
        }
        // 心跳已发出但超过 timeoutSec 仍无响应 → 进程无响应
        if (m_control->procHeartbeatPending(path)
            && sinceHeartbeat >= (m_heartbeatSec + m_timeoutSec)) {
            trigger(path, QStringLiteral("heartbeat"));
            continue;
        }
    }
}

void WatchDog::trigger(const QString &path, const QString &reason)
{
    const QString name = m_control->startName(path);
    m_control->appendConsole(path, QStringLiteral("[HMCSM] 看门狗检测到服务器无响应（%1），强制终止并纠错重启…").arg(reason));
    emit triggered(path, reason, m_control->consoleTail(path));
    // 不插入 m_intentionalKill：让 onFinished 判定为异常退出 → 走纠错重拉起
    m_control->forceKillSilent(path);
}
