#include "autocorrection.h"
#include "servercontrol.h"

#include <QDateTime>
#include <QSettings>

AutoCorrection::AutoCorrection(ServerControl *control, QObject *parent)
    : QObject(parent), m_control(control)
{
    QSettings s;
    m_autoRestart = s.value(QStringLiteral("server/autoRestart"), true).toBool();
    m_maxRetries = s.value(QStringLiteral("server/maxRetries"), 5).toInt();
    m_backoffSec = s.value(QStringLiteral("server/backoffSec"), 5).toInt();
    // 异常重试倒计时：每秒刷新 nextRetryInSec，到期触发自动重拉起
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &AutoCorrection::errorTick);
    m_timer->start();
}

void AutoCorrection::setAutoRestart(bool v)
{
    if (v == m_autoRestart)
        return;
    m_autoRestart = v;
    QSettings().setValue(QStringLiteral("server/autoRestart"), v);
    emit autoRestartChanged();
}

void AutoCorrection::setMaxRetries(int v)
{
    if (v == m_maxRetries)
        return;
    m_maxRetries = v;
    QSettings().setValue(QStringLiteral("server/maxRetries"), v);
    emit maxRetriesChanged();
}

void AutoCorrection::setBackoffSec(int v)
{
    if (v == m_backoffSec)
        return;
    m_backoffSec = v;
    QSettings().setValue(QStringLiteral("server/backoffSec"), v);
    emit backoffSecChanged();
}

void AutoCorrection::onCrash(const QString &path, const QString &logTail)
{
    // 异常分类：日志含 eula 字样 → EULA 未同意（致命、不可自动恢复）；否则视为崩溃
    const QString type = logTail.contains(QLatin1String("eula"), Qt::CaseInsensitive)
            ? QStringLiteral("eula") : QStringLiteral("crash");
    recordError(path, type, logTail);
}

void AutoCorrection::onWatchdog(const QString &path, const QString &reason, const QString &tail)
{
    recordError(path, reason, tail);
}

void AutoCorrection::onStarted(const QString &path)
{
    // 启动成功 → 该服务器的异常已纠错，清除记录
    if (m_errors.remove(path)) {
        m_retryCount.remove(path);
        emit errorRecordsChanged();
    }
}

void AutoCorrection::recordError(const QString &path, const QString &type, const QString &logTail)
{
    ErrorInfo &e = m_errors[path];
    const QString name = m_control->startName(path);
    if (e.path.isEmpty()) {
        // 首次记录：填充静态字段
        e.name = name;
        e.path = path;
        e.time = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        e.autoRestart = m_autoRestart;
        e.maxRetries = m_maxRetries;
        e.retryCount = m_retryCount.value(path, 0);
    } else {
        e.retryCount = m_retryCount.value(path, e.retryCount);
    }
    e.type = type;
    e.logTail = logTail;
    // 分类标签
    if (type == QLatin1String("eula"))
        e.typeLabel = QStringLiteral("EULA 未同意");
    else if (type == QLatin1String("io"))
        e.typeLabel = QStringLiteral("IO 管道卡死");
    else if (type == QLatin1String("heartbeat"))
        e.typeLabel = QStringLiteral("进程无响应（心跳超时）");
    else
        e.typeLabel = QStringLiteral("异常崩溃");

    // 致命（EULA 未同意）：不可自动恢复，停止重试
    if (type == QLatin1String("eula")) {
        e.fatal = true;
        e.retrying = false;
        e.nextRetryInSec = 0;
        e.retryDeadlineMs = 0;
        m_control->appendConsole(path, QStringLiteral("[HMCSM] 检测到 EULA 未同意，已停止自动重启。请在服务器目录下同意 eula.txt 后手动启动。"));
    } else if (m_autoRestart && e.retryCount < e.maxRetries) {
        // 计算指数退避延迟并安排下次自动重拉起
        const int attempt = e.retryCount + 1;
        const int delay = m_backoffSec * (1 << (attempt - 1));
        e.retrying = true;
        e.retryDeadlineMs = QDateTime::currentMSecsSinceEpoch() + qint64(delay) * 1000;
        e.nextRetryInSec = delay;
        e.fatal = false;
        m_control->appendConsole(path, QStringLiteral("[HMCSM] 后端异常（%1），%2 秒后自动重启（第 %3/%4 次）")
                              .arg(e.typeLabel).arg(delay).arg(attempt).arg(e.maxRetries));
    } else {
        // 自动重拉起已关闭或已达最大重试次数
        e.retrying = false;
        e.nextRetryInSec = 0;
        e.retryDeadlineMs = 0;
        e.fatal = false;
        if (!m_autoRestart)
            m_control->appendConsole(path, QStringLiteral("[HMCSM] 自动重启已关闭，异常未处理。请手动运维。"));
        else
            m_control->appendConsole(path, QStringLiteral("[HMCSM] 已达最大重试次数（%1），停止自动重启。请排查原因后手动启动。").arg(e.maxRetries));
    }
    emit errorRecordsChanged();
}

void AutoCorrection::clearErrorRecord(const QString &path)
{
    if (m_errors.remove(path)) {
        m_retryCount.remove(path);
        emit errorRecordsChanged();
    }
}

void AutoCorrection::errorTick()
{
    if (m_errors.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_errors.begin(); it != m_errors.end(); ++it) {
        ErrorInfo &e = it.value();
        if (!e.retrying || e.retryDeadlineMs == 0)
            continue;
        const qint64 remain = e.retryDeadlineMs - now;
        if (remain > 0) {
            const int sec = int((remain + 999) / 1000);
            if (sec != e.nextRetryInSec) {
                e.nextRetryInSec = sec;
                changed = true;
            }
            continue;
        }
        // 到点：触发一次自动重拉起
        e.nextRetryInSec = 0;
        e.retryDeadlineMs = 0;
        const int attempt = ++e.retryCount;
        const ServerControl::ServerStartArgs a = m_control->startArgs(e.path);
        if (a.path.isEmpty()) {
            e.retrying = false;   // 启动参数丢失（极少见），放弃
            changed = true;
            continue;
        }
        if (m_control->intentionallyKilled(e.path)) {
            e.retrying = false;   // 期间被人工强关，放弃重试
            changed = true;
            continue;
        }
        m_control->restart(e.path);
        // 若还能继续重试，安排下一轮
        if (e.retryCount < e.maxRetries) {
            const int delay = m_backoffSec * (1 << e.retryCount);
            e.retrying = true;
            e.retryDeadlineMs = now + qint64(delay) * 1000;
            e.nextRetryInSec = delay;
            m_control->appendConsole(e.path, QStringLiteral("[HMCSM] 自动重启第 %1/%2 次已发起（下次 %3 秒后）")
                                  .arg(attempt).arg(e.maxRetries).arg(delay));
        } else {
            e.retrying = false;
            m_control->appendConsole(e.path, QStringLiteral("[HMCSM] 自动重启第 %1/%2 次已发起，达上限后停止。").arg(attempt).arg(e.maxRetries));
        }
        changed = true;
    }
    if (changed)
        emit errorRecordsChanged();
}

QVariantList AutoCorrection::errorRecords() const
{
    QVariantList out;
    for (auto it = m_errors.begin(); it != m_errors.end(); ++it) {
        const ErrorInfo &e = it.value();
        QVariantMap m;
        m[QStringLiteral("name")] = e.name;
        m[QStringLiteral("path")] = e.path;
        m[QStringLiteral("type")] = e.type;
        m[QStringLiteral("typeLabel")] = e.typeLabel;
        m[QStringLiteral("time")] = e.time;
        m[QStringLiteral("logTail")] = e.logTail;
        m[QStringLiteral("autoRestart")] = e.autoRestart;
        m[QStringLiteral("retryCount")] = e.retryCount;
        m[QStringLiteral("maxRetries")] = e.maxRetries;
        m[QStringLiteral("retrying")] = e.retrying;
        m[QStringLiteral("nextRetryInSec")] = e.nextRetryInSec;
        m[QStringLiteral("fatal")] = e.fatal;
        out << m;
    }
    return out;
}

void AutoCorrection::retryNow(const QString &path)
{
    auto it = m_errors.find(path);
    if (it == m_errors.end())
        return;
    ErrorInfo &e = it.value();
    const ServerControl::ServerStartArgs a = m_control->startArgs(path);
    if (a.path.isEmpty())
        return;
    e.retryDeadlineMs = 0;
    e.nextRetryInSec = 0;
    m_control->restart(path);
    // 安排后续重试
    if (e.retryCount < e.maxRetries) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int delay = m_backoffSec * (1 << e.retryCount);
        e.retrying = true;
        e.retryDeadlineMs = now + qint64(delay) * 1000;
        e.nextRetryInSec = delay;
    } else {
        e.retrying = false;
    }
    emit errorRecordsChanged();
}

void AutoCorrection::stopRetries(const QString &path)
{
    auto it = m_errors.find(path);
    if (it == m_errors.end())
        return;
    it->retrying = false;
    it->nextRetryInSec = 0;
    it->retryDeadlineMs = 0;
    emit errorRecordsChanged();
}

void AutoCorrection::clearError(const QString &path)
{
    clearErrorRecord(path);
}
