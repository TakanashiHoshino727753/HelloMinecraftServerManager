#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QSet>

class ServerConfig;
class ServerControl;
class WatchDog;
class AutoCorrection;
class PlayerInfo;
class WorldInfo;
class ModsManager;
class CommandTools;

// ============================================================================
// ServerController（服务端控制器 —— 接口层门面）
// ----------------------------------------------------------------------------
// 按架构「Server Manager」节点聚合：Server Control / Server Information
// Processing / Server Configuration File Management / Process Construction /
// Command Tools / Player / World / Mods / WatchDog / Auto Correction。
//
// 本类只做「聚合 + 转发 + 信号透传」，不持有任何运行态；所有状态与逻辑
// 都在上面各子模块中。接口层（QML / WebUI / CLI / TLI）只认识本门面，
// 因此内部如何拆分对前端透明。
// ============================================================================
class ServerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningCountChanged)
    Q_PROPERTY(bool autoRestart READ autoRestart WRITE setAutoRestart NOTIFY autoRestartChanged)
    Q_PROPERTY(int maxRetries READ maxRetries WRITE setMaxRetries NOTIFY maxRetriesChanged)
    Q_PROPERTY(int backoffSec READ backoffSec WRITE setBackoffSec NOTIFY backoffSecChanged)
    Q_PROPERTY(bool watchdogEnabled READ watchdogEnabled WRITE setWatchdogEnabled NOTIFY watchdogEnabledChanged)
    Q_PROPERTY(int watchdogHeartbeatSec READ watchdogHeartbeatSec WRITE setWatchdogHeartbeatSec NOTIFY watchdogHeartbeatSecChanged)
    Q_PROPERTY(int watchdogSilenceSec READ watchdogSilenceSec WRITE setWatchdogSilenceSec NOTIFY watchdogSilenceSecChanged)
    Q_PROPERTY(int watchdogTimeoutSec READ watchdogTimeoutSec WRITE setWatchdogTimeoutSec NOTIFY watchdogTimeoutSecChanged)
    Q_PROPERTY(QVariantList errorRecords READ errorRecords NOTIFY errorRecordsChanged)
public:
    explicit ServerController(QObject *parent = nullptr);

    int runningCount() const;
    bool autoRestart() const;            void setAutoRestart(bool v);
    int maxRetries() const;              void setMaxRetries(int v);
    int backoffSec() const;              void setBackoffSec(int v);
    bool watchdogEnabled() const;        void setWatchdogEnabled(bool v);
    int watchdogHeartbeatSec() const;    void setWatchdogHeartbeatSec(int v);
    int watchdogSilenceSec() const;      void setWatchdogSilenceSec(int v);
    int watchdogTimeoutSec() const;      void setWatchdogTimeoutSec(int v);

    // ---- Server Control ----
    Q_INVOKABLE bool isRunning(const QString &path) const;
    Q_INVOKABLE QStringList runningServerNames() const;
    Q_INVOKABLE bool pathExists(const QString &path) const;
    Q_INVOKABLE void start(const QString &name, const QString &path,
                           const QString &javaPath = QStringLiteral("java"),
                           int minMem = 1024, int maxMem = 2048, bool resetRetry = true);
    Q_INVOKABLE void stop(const QString &path);
    Q_INVOKABLE void forceStop(const QString &path);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void send(const QString &path, const QString &cmd);
    Q_INVOKABLE QString getConsole(const QString &path) const;
    Q_INVOKABLE QStringList players(const QString &path) const;
    Q_INVOKABLE QVariantList runningServerUsages() const;

    // ---- WatchDog ----
    Q_INVOKABLE QVariantMap watchdogStatus(const QString &path) const;
    // ---- Auto Correction ----
    Q_INVOKABLE QVariantList errorRecords() const;
    Q_INVOKABLE void retryNow(const QString &path);
    Q_INVOKABLE void stopRetries(const QString &path);
    Q_INVOKABLE void clearError(const QString &path);

    // ---- Server Configuration File Management ----
    Q_INVOKABLE QVariantMap readProperties(const QString &path);
    Q_INVOKABLE void writeProperties(const QString &path, const QVariantMap &map);
    Q_INVOKABLE int serverPort(const QString &path) const;
    Q_INVOKABLE bool isPortFree(int port) const;
    Q_INVOKABLE int assignFreePort(const QString &path);

    // ---- Mods / Player / World / CommandTools 薄封装 ----
    Q_INVOKABLE bool isModded(const QString &path) const;
    Q_INVOKABLE QStringList listMods(const QString &path) const;
    Q_INVOKABLE QStringList modConflicts(const QString &path) const;
    Q_INVOKABLE QStringList offlinePlayers(const QString &path) const;
    Q_INVOKABLE QStringList listWorlds(const QString &path) const;
    Q_INVOKABLE QStringList commandCandidates(const QString &prefix) const;

signals:
    void consoleAppended(const QString &name, const QString &line);
    void stateChanged(const QString &name, bool running);
    void playersChanged(const QString &name, const QStringList &players);
    void runningCountChanged();
    void serverError(const QString &name, const QString &logTail);
    void playerJoined(const QString &name, const QString &who);
    void portConflict(const QString &name, const QString &path, int port, const QString &holder);
    void autoRestartChanged();
    void maxRetriesChanged();
    void backoffSecChanged();
    void watchdogEnabledChanged();
    void watchdogHeartbeatSecChanged();
    void watchdogSilenceSecChanged();
    void watchdogTimeoutSecChanged();
    void watchdogTriggered(const QString &name, const QString &reason);
    void errorRecordsChanged();

private:
    ServerConfig *m_config;
    ServerControl *m_control;
    WatchDog *m_watchdog;
    AutoCorrection *m_autocorrection;
    ModsManager *m_mods;
};
