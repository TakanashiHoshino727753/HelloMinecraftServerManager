#include "servercontrol.h"
#include "processconstruction.h"
#include "serverconfig.h"
#include "serverinfo.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QTimer>

ServerControl::ServerControl(ServerConfig *config, QObject *parent) : QObject(parent), m_config(config) {}

bool ServerControl::isRunning(const QString &path) const
{
    return m_procs.contains(path);
}

QStringList ServerControl::runningServerNames() const
{
    QStringList out;
    for (auto it = m_procs.begin(); it != m_procs.end(); ++it)
        out << m_args.value(it.key()).name;
    return out;
}

bool ServerControl::pathExists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

void ServerControl::start(const QString &name, const QString &path,
                          const QString &javaPath, int minMem, int maxMem,
                          bool resetRetry)
{
    // path 作为唯一身份键：同名不同目录的服务器互不串台
    if (m_procs.contains(path)) {
        emit consoleAppended(path, QStringLiteral("[HMCSM] 服务器已在运行"));
        return;
    }
    m_args[path] = {name, path, javaPath, minMem, maxMem};

    // ---- 多开端口冲突检测 ----
    const int port = m_config->serverPort(path);
    QString holder;
    bool conflict = false;
    for (auto it = m_ports.cbegin(); it != m_ports.cend(); ++it) {
        if (it.value() == port && it.key() != path) {
            holder = it.key();
            conflict = true;
            break;
        }
    }
    if (!conflict && !m_config->isPortFree(port))
        conflict = true;    // holder 留空 = 被系统其他程序占用
    if (conflict) {
        emit consoleAppended(path, QStringLiteral("[HMCSM] 端口 %1 已被%2占用，启动已取消（可自动分配空闲端口后重试）")
                                       .arg(port)
                                       .arg(holder.isEmpty() ? QStringLiteral("其他程序")
                                                             : QStringLiteral("服务器“%1”").arg(holder)));
        emit portConflict(name, path, port, holder);
        return;
    }

    // 解析 Java 路径：默认 "java" 时优先使用创建服务器时记录的版本专属 Java
    QString effectiveJava = javaPath;
    if (effectiveJava.isEmpty() || effectiveJava == QLatin1String("java")) {
        const QString stored = path + QStringLiteral("/.hmcsm/java.txt");
        QFile f(stored);
        if (f.open(QIODevice::ReadOnly)) {
            const QString p2 = QString::fromUtf8(f.readAll()).trimmed();
            if (!p2.isEmpty() && QFile::exists(p2))
                effectiveJava = p2;
        }
        // 兜底：递归扫描 {path}/ 下所有以 jvm 开头的目录里的 java.exe
        if (effectiveJava.isEmpty() || effectiveJava == QLatin1String("java")) {
            QDir baseDir(path);
            const QFileInfoList jvms = baseDir.entryInfoList(
                    QStringList() << QStringLiteral("jvm*"),
                    QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &fi : jvms) {
                QDirIterator it(fi.absoluteFilePath(),
                                QStringList() << QStringLiteral("java.exe") << QStringLiteral("java"),
                                QDir::Files, QDirIterator::Subdirectories);
                if (it.hasNext()) {
                    effectiveJava = it.next();
                    break;
                }
            }
        }
    }

    Proc p;
    p.proc = new QProcess(this);
    p.proc->setWorkingDirectory(path);
    p.proc->setProcessChannelMode(QProcess::MergedChannels);
    // Linux 下主进程若在 C locale 中，派生的 java 子进程会因路径含中文解码成 '?' 而打不开
    // server.jar / 无法写含中文目录。强制注入 UTF-8 locale（仅当环境未指定时）。
#ifdef Q_OS_LINUX
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (env.value(QStringLiteral("LC_ALL")).isEmpty() && env.value(QStringLiteral("LANG")).isEmpty())
            env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
        p.proc->setProcessEnvironment(env);
    }
#endif

    connect(p.proc, &QProcess::readyReadStandardOutput, this, [this, path]() {
        handleOutput(path);
    });
    connect(p.proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, path](int code, QProcess::ExitStatus s) { onFinished(path, code, s); });

    bool ok = false;
    QString errMsg;
    const QStringList args = ProcessConstruction::buildLaunchArgs(path, minMem, maxMem, &ok, &errMsg);
    if (!ok) {
        emit consoleAppended(path, QStringLiteral("[HMCSM] 启动失败：") + errMsg + QStringLiteral("请重新创建服务器。"));
        p.proc->deleteLater();
        return;
    }
    p.proc->start(effectiveJava, args);
    if (!p.proc->waitForStarted(5000)) {
        emit consoleAppended(path, QStringLiteral("[HMCSM] 启动失败：无法执行 ") + effectiveJava +
                                   QStringLiteral("（请确认已安装并加入 PATH）"));
        p.proc->deleteLater();
        return;
    }

    m_procs.insert(path, p);
    // 仅手动启动时重置重试计数；自动重启拉起（resetRetry=false）必须保留计数
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_startTime.insert(path, now);
    p.lastStdoutMs = now;
    p.lastHeartbeatMs = 0;
    m_ports.insert(path, port);
    m_intentionalKill.remove(path);
    m_tps[path] = 20.0;
    m_lastOverload.remove(path);
    emit consoleAppended(name, QStringLiteral("[HMCSM] 正在启动服务器…"));
    emit stateChanged(path, true);
    emit runningCountChanged();
    // 启动成功 → 该服务器的异常已纠错，由外围清除记录
    emit serverStarted(path);
    Q_UNUSED(resetRetry)
}

void ServerControl::handleOutput(const QString &path)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    QProcess *proc = it->proc;
    const QString data = QString::fromLocal8Bit(proc->readAllStandardOutput());
    // 看门狗：任何 stdout 输出都刷新"最后收到输出时间"，并清除待响应心跳标记
    it->lastStdoutMs = QDateTime::currentMSecsSinceEpoch();
    if (it->heartbeatPending)
        it->heartbeatPending = false;
    QStringList lines = data.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        it->console.append(line + QLatin1Char('\n'));
        if (it->console.size() > 200000)
            it->console = it->console.right(200000);
        emit consoleAppended(path, line);

        // 在线玩家追踪
        static const QRegularExpression reJoin(QStringLiteral("(\\S+) joined the game"));
        static const QRegularExpression reLeft(QStringLiteral("(\\S+) left the game"));
        QRegularExpressionMatch m = reJoin.match(line);
        if (m.hasMatch()) {
            const QString who = m.captured(1);
            if (!it->playerList.contains(who)) {
                it->playerList.append(who);
                emit playersChanged(path, it->playerList);
                emit playerJoined(path, who);
            }
        } else if ((m = reLeft.match(line)).hasMatch()) {
            const QString who = m.captured(1);
            if (it->playerList.removeAll(who) > 0)
                emit playersChanged(path, it->playerList);
        }

        // A2：资源监控 - 估算 TPS
        static const QRegularExpression reTps(QStringLiteral(
            "Can't keep up.*?Running\\s+(\\d+)ms\\s+behind"),
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch tm = reTps.match(line);
        if (tm.hasMatch()) {
            const double behind = tm.captured(1).toDouble();
            const double tps = qBound(0.0, 20000.0 / (20000.0 + behind), 20.0);
            m_tps[path] = tps;
            m_lastOverload[path] = QDateTime::currentMSecsSinceEpoch();
        }
    }
}

void ServerControl::onFinished(const QString &path, int exitCode, QProcess::ExitStatus status)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    const QString tail = it->console.right(3000);
    const bool intentional = m_intentionalKill.remove(path);
    it->proc->deleteLater();
    m_consoleCache.insert(path, it->console);
    m_procs.erase(it);
    m_startTime.remove(path);
    m_ports.remove(path);
    m_usageSamples.remove(path);
    m_tps.remove(path);
    m_lastOverload.remove(path);
    emit consoleAppended(path, QStringLiteral("[HMCSM] 服务器已停止"));
    emit stateChanged(path, false);
    emit playersChanged(path, {});
    emit runningCountChanged();
    // 非主动强关却异常退出（崩溃 / 非 0 退出码）→ 上报错误日志，由外围记入异常纠错中心
    if (!intentional && (status == QProcess::CrashExit || exitCode != 0)) {
        emit serverError(path, tail);
    }
}

void ServerControl::stop(const QString &path)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    it->proc->write("stop\n");
}

void ServerControl::forceStop(const QString &path)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    m_intentionalKill.insert(path);
    it->proc->kill();
}

void ServerControl::forceKillSilent(const QString &path)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    it->proc->kill();
}

void ServerControl::stopAll()
{
    if (m_procs.isEmpty())
        return;
    qWarning() << "[Server] 程序退出：强制停止" << m_procs.size() << "个运行中的服务器进程";
    for (auto it = m_procs.begin(); it != m_procs.end(); ++it) {
        m_intentionalKill.insert(it.key());
        QProcess *p = it->proc;
        if (p) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
    m_procs.clear();
    m_usageSamples.clear();
    m_tps.clear();
    m_lastOverload.clear();
    m_startTime.clear();
    m_ports.clear();
    emit runningCountChanged();
}

void ServerControl::send(const QString &path, const QString &cmd)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    it->proc->write(cmd.toLocal8Bit() + "\n");
}

void ServerControl::sendListHeartbeat(const QString &path)
{
    auto it = m_procs.find(path);
    if (it == m_procs.end())
        return;
    it->proc->write("list\n");
    it->lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    it->heartbeatPending = true;
}

QString ServerControl::getConsole(const QString &path) const
{
    auto it = m_procs.find(path);
    if (it != m_procs.end())
        return it->console;
    return m_consoleCache.value(path);
}

QStringList ServerControl::players(const QString &path) const
{
    auto it = m_procs.find(path);
    if (it != m_procs.end())
        return it->playerList;
    return {};
}

QString ServerControl::consoleTail(const QString &path, int n) const
{
    auto it = m_procs.find(path);
    if (it != m_procs.end())
        return it->console.right(n);
    return m_consoleCache.value(path).right(n);
}

void ServerControl::appendConsole(const QString &path, const QString &line)
{
    emit consoleAppended(path, line);
}

qint64 ServerControl::procLastStdoutMs(const QString &path) const
{
    auto it = m_procs.find(path);
    return it == m_procs.end() ? 0 : it->lastStdoutMs;
}

qint64 ServerControl::procLastHeartbeatMs(const QString &path) const
{
    auto it = m_procs.find(path);
    return it == m_procs.end() ? 0 : it->lastHeartbeatMs;
}

bool ServerControl::procHeartbeatPending(const QString &path) const
{
    auto it = m_procs.find(path);
    return it == m_procs.end() ? false : it->heartbeatPending;
}

void ServerControl::setProcHeartbeatPending(const QString &path, bool v)
{
    auto it = m_procs.find(path);
    if (it != m_procs.end())
        it->heartbeatPending = v;
}

void ServerControl::restart(const QString &path)
{
    const ServerStartArgs a = m_args.value(path);
    if (a.path.isEmpty())
        return;
    start(a.name, a.path, a.javaPath, a.minMem, a.maxMem, /*resetRetry=*/false);
}

ServerControl::ServerStartArgs ServerControl::startArgs(const QString &path) const
{
    return m_args.value(path);
}

QString ServerControl::startName(const QString &path) const
{
    return m_args.value(path).name;
}

QSet<int> ServerControl::usedPorts() const
{
    QSet<int> s;
    for (auto it = m_ports.cbegin(); it != m_ports.cend(); ++it)
        s.insert(it.value());
    return s;
}

bool ServerControl::intentionallyKilled(const QString &path) const
{
    return m_intentionalKill.contains(path);
}

QVariantList ServerControl::runningServerUsages() const
{
    QVariantList out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 多核机器上"进程占用全部核心"的 CPU 时间换算为 0–100% 的单核等效占用率
    const int coreCount = qMax(1, QThread::idealThreadCount());
    for (auto it = m_procs.constBegin(); it != m_procs.constEnd(); ++it) {
        const QString name = it.key();
        const QProcess *proc = it->proc;
        QVariantMap m;
        m[QStringLiteral("path")] = name;
        m[QStringLiteral("players")] = it->playerList.size();
        const qint64 start = m_startTime.value(name, now);
        m[QStringLiteral("uptimeSec")] = start ? (now - start) / 1000 : 0;
        // A2：若过去 60s 内没有过载日志，视为满速 20 TPS
        double tps = 20.0;
        const qint64 lastOver = m_lastOverload.value(name, 0);
        if (lastOver > 0 && (now - lastOver) < 60000)
            tps = m_tps.value(name, 20.0);
        m[QStringLiteral("tps")] = tps;
        const QVariantMap usage = ServerInfo::sampleCpuMem(proc, m_usageSamples[name], coreCount);
        m[QStringLiteral("memMB")] = usage[QStringLiteral("memMB")];
        m[QStringLiteral("cpu")] = usage[QStringLiteral("cpu")];
        out << m;
    }
    return out;
}
