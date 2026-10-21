#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QTimer>

class ServerControl;

// ============================================================================
// Auto Correction（异常纠错中心）：崩溃 / 看门狗卡死的自动重拉起与人工干预
// ----------------------------------------------------------------------------
// 持有活动异常记录（ErrorInfo）、重试配置（自动重启/最大重试/退避基数）与
// 每秒倒计时定时器。崩溃（来自 ServerControl::serverError）与看门狗卡死
// （来自 WatchDog::triggered）统一汇入 recordError 分类调度；重试通过
// ServerControl 用已存启动参数重拉起。
// ============================================================================
class AutoCorrection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoRestart READ autoRestart WRITE setAutoRestart NOTIFY autoRestartChanged)
    Q_PROPERTY(int maxRetries READ maxRetries WRITE setMaxRetries NOTIFY maxRetriesChanged)
    Q_PROPERTY(int backoffSec READ backoffSec WRITE setBackoffSec NOTIFY backoffSecChanged)
    Q_PROPERTY(QVariantList errorRecords READ errorRecords NOTIFY errorRecordsChanged)
public:
    explicit AutoCorrection(ServerControl *control, QObject *parent = nullptr);

    bool autoRestart() const { return m_autoRestart; }
    void setAutoRestart(bool v);
    int maxRetries() const { return m_maxRetries; }
    void setMaxRetries(int v);
    int backoffSec() const { return m_backoffSec; }
    void setBackoffSec(int v);

    Q_INVOKABLE QVariantList errorRecords() const;
    Q_INVOKABLE void retryNow(const QString &path);
    Q_INVOKABLE void stopRetries(const QString &path);
    Q_INVOKABLE void clearError(const QString &path);

signals:
    void autoRestartChanged();
    void maxRetriesChanged();
    void backoffSecChanged();
    void errorRecordsChanged();

public slots:
    // 进程异常退出（来自 ServerControl::serverError）
    void onCrash(const QString &path, const QString &logTail);
    // 看门狗触发（来自 WatchDog::triggered）
    void onWatchdog(const QString &path, const QString &reason, const QString &tail);
    // 启动成功（来自 ServerControl::serverStarted）：清除该服务器异常记录（视为已纠错）
    void onStarted(const QString &path);

private:
    void recordError(const QString &path, const QString &type, const QString &logTail);
    void clearErrorRecord(const QString &path);
    void errorTick();

    struct ErrorInfo {
        QString name;
        QString path;
        QString type;            // crash / io / heartbeat / eula
        QString typeLabel;       // 中文分类标签
        QString time;            // 首次发生时间（本地可读）
        QString logTail;         // 尾部日志（供界面展示）
        bool autoRestart = true; // 是否配置了自动重拉起
        int retryCount = 0;      // 已重试次数
        int maxRetries = 5;      // 最大重试次数
        bool retrying = false;   // 仍在自动重拉起尝试中（false = 已停止重试或已放弃）
        int nextRetryInSec = 0;  // 距下次自动重拉起的剩余秒数（>0 表示 pending）
        bool fatal = false;      // 致命且不可自动恢复（如 eula 未同意）
        qint64 retryDeadlineMs = 0; // 计划重拉起的墙钟时刻
    };

    ServerControl *m_control;
    QHash<QString, ErrorInfo> m_errors;
    QMap<QString, int> m_retryCount;
    bool m_autoRestart = true;
    int m_maxRetries = 5;
    int m_backoffSec = 5;
    QTimer *m_timer = nullptr;
};
