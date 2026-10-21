#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QDateTime>
#include <QProcess>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

class ServerConfig;

// ============================================================================
// Server Control（服务端控制）：单台服务端进程的运行时管理
// ----------------------------------------------------------------------------
// 负责启动 / 停止 / 强停 / 发送指令、捕获控制台输出、跟踪在线玩家、
// 资源占用采样、端口冲突检测。进程运行态（Proc）由本类独占持有。
// 看门狗与异常纠错通过本类暴露的访问器观察/命令进程，不直接持有 Proc。
// ============================================================================
class ServerControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningCountChanged)
public:
    struct ServerStartArgs {
        QString name;
        QString path;
        QString javaPath;
        int minMem = 1024;
        int maxMem = 2048;
    };

    explicit ServerControl(ServerConfig *config, QObject *parent = nullptr);
    int runningCount() const { return m_procs.size(); }

    // ---- Server Control ----
    Q_INVOKABLE bool isRunning(const QString &path) const;
    Q_INVOKABLE QStringList runningServerNames() const;
    Q_INVOKABLE bool pathExists(const QString &path) const;
    void start(const QString &name, const QString &path,
               const QString &javaPath = QStringLiteral("java"),
               int minMem = 1024, int maxMem = 2048,
               bool resetRetry = true);
    Q_INVOKABLE void stop(const QString &path);
    Q_INVOKABLE void forceStop(const QString &path);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void send(const QString &path, const QString &cmd);
    Q_INVOKABLE QString getConsole(const QString &path) const;
    Q_INVOKABLE QStringList players(const QString &path) const;
    Q_INVOKABLE QVariantList runningServerUsages() const;

    // ---- 供 WatchDog / AutoCorrection 访问的进程运行态 ----
    qint64 procLastStdoutMs(const QString &path) const;
    qint64 procLastHeartbeatMs(const QString &path) const;
    bool procHeartbeatPending(const QString &path) const;
    void setProcHeartbeatPending(const QString &path, bool v);
    void sendListHeartbeat(const QString &path);
    // 强关但不标记 intentionalKill，使 onFinished 视为异常崩溃（走纠错重拉起）
    void forceKillSilent(const QString &path);
    // 用已存启动参数重拉起（resetRetry=false）
    void restart(const QString &path);
    ServerStartArgs startArgs(const QString &path) const;
    QString startName(const QString &path) const;
    QSet<int> usedPorts() const;
    // 该 path 是否已被人工强关（自动重拉起前应放弃）
    bool intentionallyKilled(const QString &path) const;
    // 最近 console 尾部（看门狗记录异常用）
    QString consoleTail(const QString &path, int n = 3000) const;
    // 向控制台追加一行（看门狗/纠错中心用来回显提示）
    void appendConsole(const QString &path, const QString &line);

signals:
    void consoleAppended(const QString &name, const QString &line);
    void stateChanged(const QString &name, bool running);
    void playersChanged(const QString &name, const QStringList &players);
    void runningCountChanged();
    void serverError(const QString &name, const QString &logTail);
    void playerJoined(const QString &name, const QString &who);
    void portConflict(const QString &name, const QString &path, int port, const QString &holder);
    // 启动成功：异常已纠错，外围可清除该服务器记录
    void serverStarted(const QString &path);

private:
    void handleOutput(const QString &path);
    void onFinished(const QString &path, int exitCode, QProcess::ExitStatus status);

    struct Proc {
        QProcess *proc = nullptr;
        QString console;
        QStringList playerList;
        qint64 lastStdoutMs = 0;
        qint64 lastHeartbeatMs = 0;
        bool heartbeatPending = false;
    };
    QHash<QString, Proc> m_procs;
    QHash<QString, QString> m_consoleCache;
    QHash<QString, qint64> m_startTime;
    QHash<QString, int> m_ports;
    mutable QHash<QString, QPair<qint64, qint64>> m_usageSamples;   // 采样差分状态，允许在 const 采样接口内更新
    QSet<QString> m_intentionalKill;
    QHash<QString, ServerStartArgs> m_args;
    QMap<QString, double> m_tps;
    QMap<QString, qint64> m_lastOverload;
    ServerConfig *m_config;
};
