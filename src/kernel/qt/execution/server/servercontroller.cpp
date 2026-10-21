#include "servercontroller.h"
#include "serverconfig.h"
#include "servercontrol.h"
#include "watchdog.h"
#include "autocorrection.h"
#include "playerinfo.h"
#include "worldinfo.h"
#include "modsmanager.h"
#include "commandtools.h"

ServerController::ServerController(QObject *parent) : QObject(parent)
{
    // 子模块按依赖顺序构造：配置 → 控制 → 看门狗/纠错（均依赖控制）
    m_config = new ServerConfig(this);
    m_control = new ServerControl(m_config, this);
    m_watchdog = new WatchDog(m_control, this);
    m_autocorrection = new AutoCorrection(m_control, this);
    m_mods = new ModsManager(this);
    // PlayerInfo / WorldInfo / CommandTools 为无状态静态工具类，无需实例化，直接调用静态方法

    // 透传 Server Control 的信号
    connect(m_control, &ServerControl::consoleAppended, this, &ServerController::consoleAppended);
    connect(m_control, &ServerControl::stateChanged, this, &ServerController::stateChanged);
    connect(m_control, &ServerControl::playersChanged, this, &ServerController::playersChanged);
    connect(m_control, &ServerControl::runningCountChanged, this, &ServerController::runningCountChanged);
    connect(m_control, &ServerControl::playerJoined, this, &ServerController::playerJoined);
    connect(m_control, &ServerControl::portConflict, this, &ServerController::portConflict);

    // 崩溃 → 异常纠错中心
    connect(m_control, &ServerControl::serverError, m_autocorrection, &AutoCorrection::onCrash);
    // 启动成功 → 清除该服务器异常记录（视为已纠错）
    connect(m_control, &ServerControl::serverStarted, m_autocorrection, &AutoCorrection::onStarted);

    // 看门狗触发 → 记入纠错中心 + 透传看门狗触发信号（用服务器名）
    connect(m_watchdog, &WatchDog::triggered, this, [this](const QString &path, const QString &reason, const QString &) {
        m_autocorrection->onWatchdog(path, reason, m_control->consoleTail(path));
        emit watchdogTriggered(m_control->startName(path), reason);
    });

    // 纠错中心 / 看门狗的属性变更透传，保持门面属性与子模块同步
    connect(m_autocorrection, &AutoCorrection::errorRecordsChanged, this, &ServerController::errorRecordsChanged);
    connect(m_autocorrection, &AutoCorrection::autoRestartChanged, this, &ServerController::autoRestartChanged);
    connect(m_autocorrection, &AutoCorrection::maxRetriesChanged, this, &ServerController::maxRetriesChanged);
    connect(m_autocorrection, &AutoCorrection::backoffSecChanged, this, &ServerController::backoffSecChanged);
    connect(m_watchdog, &WatchDog::enabledChanged, this, &ServerController::watchdogEnabledChanged);
    connect(m_watchdog, &WatchDog::heartbeatSecChanged, this, &ServerController::watchdogHeartbeatSecChanged);
    connect(m_watchdog, &WatchDog::silenceSecChanged, this, &ServerController::watchdogSilenceSecChanged);
    connect(m_watchdog, &WatchDog::timeoutSecChanged, this, &ServerController::watchdogTimeoutSecChanged);
}

// ---- 属性转发 ----
int ServerController::runningCount() const { return m_control->runningCount(); }
bool ServerController::autoRestart() const { return m_autocorrection->autoRestart(); }
void ServerController::setAutoRestart(bool v) { m_autocorrection->setAutoRestart(v); }
int ServerController::maxRetries() const { return m_autocorrection->maxRetries(); }
void ServerController::setMaxRetries(int v) { m_autocorrection->setMaxRetries(v); }
int ServerController::backoffSec() const { return m_autocorrection->backoffSec(); }
void ServerController::setBackoffSec(int v) { m_autocorrection->setBackoffSec(v); }
bool ServerController::watchdogEnabled() const { return m_watchdog->enabled(); }
void ServerController::setWatchdogEnabled(bool v) { m_watchdog->setEnabled(v); }
int ServerController::watchdogHeartbeatSec() const { return m_watchdog->heartbeatSec(); }
void ServerController::setWatchdogHeartbeatSec(int v) { m_watchdog->setHeartbeatSec(v); }
int ServerController::watchdogSilenceSec() const { return m_watchdog->silenceSec(); }
void ServerController::setWatchdogSilenceSec(int v) { m_watchdog->setSilenceSec(v); }
int ServerController::watchdogTimeoutSec() const { return m_watchdog->timeoutSec(); }
void ServerController::setWatchdogTimeoutSec(int v) { m_watchdog->setTimeoutSec(v); }

// ---- Server Control 转发 ----
bool ServerController::isRunning(const QString &path) const { return m_control->isRunning(path); }
QStringList ServerController::runningServerNames() const { return m_control->runningServerNames(); }
bool ServerController::pathExists(const QString &path) const { return m_control->pathExists(path); }
void ServerController::start(const QString &name, const QString &path, const QString &javaPath,
                             int minMem, int maxMem, bool resetRetry)
{
    if (resetRetry)
        m_autocorrection->clearError(path);   // 全新启动：重置该服务器纠错计数
    m_control->start(name, path, javaPath, minMem, maxMem, resetRetry);
}
void ServerController::stop(const QString &path) { m_control->stop(path); }
void ServerController::forceStop(const QString &path) { m_control->forceStop(path); }
void ServerController::stopAll() { m_control->stopAll(); }
void ServerController::send(const QString &path, const QString &cmd) { m_control->send(path, cmd); }
QString ServerController::getConsole(const QString &path) const { return m_control->getConsole(path); }
QStringList ServerController::players(const QString &path) const { return m_control->players(path); }
QVariantList ServerController::runningServerUsages() const { return m_control->runningServerUsages(); }

// ---- WatchDog / Auto Correction 转发 ----
QVariantMap ServerController::watchdogStatus(const QString &path) const { return m_watchdog->status(path); }
QVariantList ServerController::errorRecords() const { return m_autocorrection->errorRecords(); }
void ServerController::retryNow(const QString &path) { m_autocorrection->retryNow(path); }
void ServerController::stopRetries(const QString &path) { m_autocorrection->stopRetries(path); }
void ServerController::clearError(const QString &path) { m_autocorrection->clearError(path); }

// ---- Server Configuration File Management 转发 ----
QVariantMap ServerController::readProperties(const QString &path) { return m_config->readProperties(path); }
void ServerController::writeProperties(const QString &path, const QVariantMap &map) { m_config->writeProperties(path, map); }
int ServerController::serverPort(const QString &path) const { return m_config->serverPort(path); }
bool ServerController::isPortFree(int port) const { return m_config->isPortFree(port); }
int ServerController::assignFreePort(const QString &path) { return m_config->assignFreePort(path, m_control->usedPorts()); }

// ---- Mods / Player / World / CommandTools 薄封装 ----
bool ServerController::isModded(const QString &path) const { return m_mods->isModded(path); }
QStringList ServerController::listMods(const QString &path) const { return m_mods->listMods(path); }
QStringList ServerController::modConflicts(const QString &path) const { return m_mods->conflictExclusion(path); }
QStringList ServerController::offlinePlayers(const QString &path) const { return PlayerInfo::acquireOffline(path); }
QStringList ServerController::listWorlds(const QString &path) const { return WorldInfo::listWorlds(path); }
QStringList ServerController::commandCandidates(const QString &prefix) const { return CommandTools::complete(prefix); }
