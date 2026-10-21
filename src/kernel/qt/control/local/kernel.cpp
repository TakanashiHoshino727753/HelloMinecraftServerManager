#include "kernel.h"

#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include "hmcsm_logging.h"

namespace {
// ModuleBus 中的注册名。刻意与 QML 上下文属性名保持一致：
// 本地直连（LocalConnection）把服务暴露给接口层时，两套名字共用同一份真相，
// 接口层无需再维护一份「名字 -> 对象」的映射表。
constexpr const char *kServerManager = "serverManager";
constexpr const char *kServerController = "serverController";
constexpr const char *kSystemMonitor = "systemMonitor";
constexpr const char *kDownloadManager = "downloadManager";
constexpr const char *kDownloadCatalog = "downloadCatalog";
constexpr const char *kJavaManager = "javaManager";
constexpr const char *kInstallCoordinator = "installCoordinator";
constexpr const char *kCreateServer = "createServer";
constexpr const char *kImportModpack = "importModpack";
constexpr const char *kSettingsController = "settingsController";
constexpr const char *kAppearance = "appearance";
constexpr const char *kWebuiServer = "webuiServer";
constexpr const char *kBotController = "botController";
constexpr const char *kProxyController = "proxyController";
constexpr const char *kProxyManager = "proxyManager";
constexpr const char *kPortMapper = "portMapper";
constexpr const char *kBackupController = "backupController";
constexpr const char *kSchedulerController = "schedulerController";
constexpr const char *kUpdateController = "updateController";
constexpr const char *kRemoteControl = "remoteControl";
constexpr const char *kDiskAccess = "diskAccess";
constexpr const char *kProcessPrivilege = "processPrivilege";
constexpr const char *kResourceAllocator = "resourceAllocator";
} // namespace

Kernel::Kernel(QObject *parent) : QObject(parent)
{
    // 共享外观模型的皮肤目录按应用区分（桌面端 tag=HMCSM，与旧版路径完全一致，无需迁移）。
    // 必须在 AppearanceSettings 实例化之前设置。
    AppearanceSettings::setAppTag(QStringLiteral("HMCSM"));

    // ===== Management：先于所有执行模块构造，供其查询资源与权限 =====
    m_bus = std::make_unique<ModuleBus>(this);
    m_diskAccess = std::make_unique<DiskAccess>(this);
    m_processPrivilege = std::make_unique<ProcessPrivilege>(this);
    m_resourceAllocator = std::make_unique<ResourceAllocator>(this);

    // ===== Execution =====
    m_serverManager = std::make_unique<ServerManager>(this);
    m_systemMonitor = std::make_unique<SystemMonitor>(this);
    m_downloadManager = std::make_unique<DownloadManager>(this);
    // Java 运行环境管理：按 MC 版本选择并自动下载安装合适的 JDK。
    // 显式注入统一下载管理器，Java 下载才能进入下载面板（并行/进度/暂停/取消/超时）。
    m_javaManager = std::make_unique<JavaManager>(m_downloadManager.get(), this);
    m_downloadCatalog = std::make_unique<DownloadCatalog>(m_downloadManager.get(), this);
    m_downloadCatalog->setJavaManager(m_javaManager.get());
    // 下载中心「模组服」安装到服务端：为每个加载器新建独立服务端实例，复用已验证安装流程
    m_installCoordinator = std::make_unique<InstallCoordinator>(m_downloadManager.get(),
                                                               m_serverManager.get(),
                                                               m_javaManager.get(), this);
    m_createServer = std::make_unique<CreateServerController>(m_downloadManager.get(),
                                                              m_serverManager.get(),
                                                              m_javaManager.get(), this);
    m_importModpack = std::make_unique<ModpackImporter>(m_downloadManager.get(),
                                                        m_serverManager.get(), this);
    m_serverController = std::make_unique<ServerController>(this);
    m_settingsController = std::make_unique<SettingsController>(this);
    // 共享外观个性化模型：唯一数据源，SettingsController 仅做委托。
    m_appearance = std::make_unique<AppearanceSettings>(this);
    m_settingsController->setAppearance(m_appearance.get());
    m_proxyManager = std::make_unique<ProxyManager>(m_serverManager.get(),
                                                    m_serverController.get(),
                                                    m_javaManager.get(), this);
    m_portMapper = std::make_unique<PortMapper>(this);
    m_backupController = std::make_unique<BackupController>(this);
    m_schedulerController = std::make_unique<SchedulerController>(m_serverManager.get(),
                                                                  m_serverController.get(),
                                                                  m_backupController.get(), this);
    m_updateController = std::make_unique<UpdateController>(this);
    m_notifier = std::make_unique<Notifier>(this);

    // ===== Control / Remote =====
    m_webuiServer = std::make_unique<WebUIServer>(m_serverManager.get(), m_serverController.get(),
                                                  m_downloadManager.get(), m_createServer.get(),
                                                  m_importModpack.get(), m_settingsController.get(),
                                                  m_systemMonitor.get(), m_javaManager.get(), this);
    m_botController = std::make_unique<BotController>(m_serverManager.get(), m_serverController.get(),
                                                      m_settingsController.get(),
                                                      m_downloadManager.get(), this);
    m_remoteControl = std::make_unique<RemoteControlServer>(m_settingsController.get(), this);
}

Kernel::~Kernel() = default;

void Kernel::start()
{
    // ===== 服务注册：模块之间按名互相发现，不再互相持有裸指针 =====
    m_bus->registerService(QLatin1String(kServerManager), m_serverManager.get());
    m_bus->registerService(QLatin1String(kServerController), m_serverController.get());
    m_bus->registerService(QLatin1String(kSystemMonitor), m_systemMonitor.get());
    m_bus->registerService(QLatin1String(kDownloadManager), m_downloadManager.get());
    m_bus->registerService(QLatin1String(kDownloadCatalog), m_downloadCatalog.get());
    m_bus->registerService(QLatin1String(kJavaManager), m_javaManager.get());
    m_bus->registerService(QLatin1String(kInstallCoordinator), m_installCoordinator.get());
    m_bus->registerService(QLatin1String(kCreateServer), m_createServer.get());
    m_bus->registerService(QLatin1String(kImportModpack), m_importModpack.get());
    m_bus->registerService(QLatin1String(kSettingsController), m_settingsController.get());
    m_bus->registerService(QLatin1String(kAppearance), m_appearance.get());
    m_bus->registerService(QLatin1String(kProxyController), m_proxyManager->defaultProxy());
    m_bus->registerService(QLatin1String(kProxyManager), m_proxyManager.get());
    m_bus->registerService(QLatin1String(kPortMapper), m_portMapper.get());
    m_bus->registerService(QLatin1String(kBackupController), m_backupController.get());
    m_bus->registerService(QLatin1String(kSchedulerController), m_schedulerController.get());
    m_bus->registerService(QLatin1String(kUpdateController), m_updateController.get());
    m_bus->registerService(QLatin1String(kWebuiServer), m_webuiServer.get());
    m_bus->registerService(QLatin1String(kBotController), m_botController.get());
    m_bus->registerService(QLatin1String(kRemoteControl), m_remoteControl.get());
    m_bus->registerService(QLatin1String(kDiskAccess), m_diskAccess.get());
    m_bus->registerService(QLatin1String(kProcessPrivilege), m_processPrivilege.get());
    m_bus->registerService(QLatin1String(kResourceAllocator), m_resourceAllocator.get());

    wireSettings();
    wireEvents();
    wireControl();

    hmcsmAppendLog(QByteArrayLiteral("[INF] kernel: started, services=")
                 + QByteArray::number(m_bus->serviceNames().size()));

    emit started();
}

void Kernel::shutdown()
{
    // 退出前释放所有运行中的子进程，避免服务器 / bot / 代理进程残留。
    m_serverController->stopAll();
    ProxyController *proxy = m_proxyManager->defaultProxy();
    if (proxy && proxy->running())
        proxy->stop();
    m_botController->stopAll();
}

// ---------------------------------------------------------------------------
// 设置变更 -> 各执行模块
// ---------------------------------------------------------------------------
void Kernel::wireSettings()
{
    SettingsController *sc = m_settingsController.get();

    // 服务器扫描只针对「默认服务器目录」，不递归整个文档目录。
    m_serverManager->setDefaultServerDir(sc->defaultServerDir());
    connect(sc, &SettingsController::defaultServerDirChanged, this, [this]() {
        m_serverManager->setDefaultServerDir(m_settingsController->defaultServerDir());
    });

    // 受管目录纳入磁盘访问白名单（内核只允许在这些目录内读写）
    m_diskAccess->addRoot(sc->defaultServerDir());
    m_diskAccess->addRoot(DownloadManager::defaultDownloadDir());

    // ---- 机器人 ----
    BotController *bot = m_botController.get();
    bot->setNapcatPath(sc->napcatPath());
    bot->setNonebotDir(sc->nonebotDir());
    bot->setUsageInterval(sc->botUsageInterval());
    bot->setBotLinkedStart(sc->botLinkedStart());
    bot->setBotEnabled(sc->botEnabled());
    connect(sc, &SettingsController::botEnabledChanged, bot,
            [this, bot]() { bot->setBotEnabled(m_settingsController->botEnabled()); });
    connect(sc, &SettingsController::napcatPathChanged, bot,
            [this, bot]() { bot->setNapcatPath(m_settingsController->napcatPath()); });
    connect(sc, &SettingsController::nonebotDirChanged, bot,
            [this, bot]() { bot->setNonebotDir(m_settingsController->nonebotDir()); });
    connect(sc, &SettingsController::botUsageIntervalChanged, bot,
            [this, bot]() { bot->setUsageInterval(m_settingsController->botUsageInterval()); });
    connect(sc, &SettingsController::botLinkedStartChanged, bot,
            [this, bot]() { bot->setBotLinkedStart(m_settingsController->botLinkedStart()); });

    // ---- Webhook ----
    auto syncNotifier = [this]() {
        SettingsController *s = m_settingsController.get();
        m_notifier->setUrl(s->webhookUrl());
        m_notifier->setType(s->webhookType());
        m_notifier->setEnabled(s->webhookEnabled());
        m_notifier->setNotifyCrash(s->webhookCrash());
        m_notifier->setNotifyState(s->webhookState());
        m_notifier->setNotifyPlayer(s->webhookPlayer());
    };
    syncNotifier();
    connect(sc, &SettingsController::webhookUrlChanged, this, syncNotifier);
    connect(sc, &SettingsController::webhookTypeChanged, this, syncNotifier);
    connect(sc, &SettingsController::webhookEnabledChanged, this, syncNotifier);
    connect(sc, &SettingsController::webhookCrashChanged, this, syncNotifier);
    connect(sc, &SettingsController::webhookStateChanged, this, syncNotifier);
    connect(sc, &SettingsController::webhookPlayerChanged, this, syncNotifier);

    // ---- 备份：受管服务器列表同步 ----
    auto syncBackupServers = [this]() {
        m_backupController->setServerList(m_serverManager->serverSummary());
    };
    syncBackupServers();
    connect(m_serverManager.get(), &ServerManager::serversChanged, this, syncBackupServers);

    // 启动即备份（用户开启时）：遍历所有受管服务器各打一份初始备份
    if (m_backupController->onStart()) {
        for (const QVariant &v : m_serverManager->serverSummary()) {
            const QVariantMap m = v.toMap();
            m_backupController->backupNow(m.value(QStringLiteral("name")).toString(),
                                          m.value(QStringLiteral("path")).toString());
        }
    }
}

// ---------------------------------------------------------------------------
// 执行模块事件 -> Webhook 通知 / QQ 机器人
// ---------------------------------------------------------------------------
void Kernel::wireEvents()
{
    ServerController *srv = m_serverController.get();
    Notifier *notifier = m_notifier.get();
    BotController *bot = m_botController.get();
    ProxyController *proxy = m_proxyManager->defaultProxy();

    // ---- 后端事件 -> Webhook ----
    connect(srv, &ServerController::serverError, this,
            [this, notifier](const QString &name, const QString &) {
                if (m_notifier->notifyCrash())
                    notifier->send(QStringLiteral("服务器异常退出"),
                                   name + QStringLiteral(" 异常退出，已尝试自动重启"));
            });
    connect(srv, &ServerController::stateChanged, this,
            [this, notifier](const QString &name, bool running) {
                if (m_notifier->notifyState())
                    notifier->send(running ? QStringLiteral("服务器已启动") : QStringLiteral("服务器已停止"), name);
            });
    connect(srv, &ServerController::playerJoined, this,
            [this, notifier](const QString &name, const QString &who) {
                if (m_notifier->notifyPlayer())
                    notifier->send(QStringLiteral("玩家进服"), who + QStringLiteral(" 进入了 ") + name);
            });

    // ---- 代理事件 -> Webhook ----
    connect(proxy, &ProxyController::crashed, this, [this, notifier]() {
        if (m_notifier->notifyCrash())
            notifier->send(QStringLiteral("代理异常退出"),
                           QStringLiteral("Velocity 代理异常退出，已尝试自动重启"));
    });
    connect(proxy, &ProxyController::runningChanged, this, [this, notifier, proxy]() {
        if (m_notifier->notifyState())
            notifier->send(proxy->running() ? QStringLiteral("代理已启动") : QStringLiteral("代理已停止"),
                           QStringLiteral("Velocity 代理"));
    });

    // ---- 一键更新 jar -> Webhook ----
    connect(m_updateController.get(), &UpdateController::updateFinished, this,
            [this, notifier](const QString &name, bool ok, const QString &msg, const QString &) {
                if (m_notifier->enabled())
                    notifier->send(ok ? QStringLiteral("服务端更新完成") : QStringLiteral("服务端更新失败"),
                                   name + QStringLiteral("：") + msg);
            });

    // ---- HMCSM -> QQ ----
    // 服务器异常退出：把日志私信管理员（不主动推送启停事件）
    connect(srv, &ServerController::serverError, this,
            [bot](const QString &name, const QString &tail) { bot->pushError(name, tail); });
    // 状态推送（Q2）：服务器启停 / 代理启停 同步到 QQ 群
    connect(srv, &ServerController::stateChanged, this,
            [bot](const QString &name, bool running) {
                bot->notify(QStringLiteral("[HMCSM] 服务器 %1 %2")
                                .arg(name, running ? QStringLiteral("已启动") : QStringLiteral("已停止")),
                            QStringLiteral("group"));
            });
    connect(proxy, &ProxyController::runningChanged, this, [bot, proxy]() {
        bot->notify(QStringLiteral("[HMCSM] 代理 %1")
                        .arg(proxy->running() ? QStringLiteral("已启动") : QStringLiteral("已停止")),
                    QStringLiteral("group"));
    });

    // ---- 安装协调器 -> 下载中心状态条 ----
    connect(m_installCoordinator.get(), &InstallCoordinator::toast, this,
            [this](const QString &title, const QString &text) {
                m_downloadCatalog->setStatus(title + QStringLiteral("：") + text);
            });
}

// ---------------------------------------------------------------------------
// 控制层：WebUI / 远控配对 / 机器人控制通道
// ---------------------------------------------------------------------------
void Kernel::wireControl()
{
    SettingsController *sc = m_settingsController.get();
    WebUIServer *webui = m_webuiServer.get();

    // WebUI 增强：接入代理管理器与主下载目录（优化模组）
    webui->setProxyManager(m_proxyManager.get());
    webui->setDownloadCatalog(m_downloadCatalog.get());
    webui->setInstallCoordinator(m_installCoordinator.get());
    webui->setBotController(m_botController.get());
    webui->setBackupController(m_backupController.get());
    webui->setSchedulerController(m_schedulerController.get());
    webui->setUpdateController(m_updateController.get());
    webui->setPortMapper(m_portMapper.get());
    webui->setEnabled(sc->webuiEnabled());
    webui->setPort(sc->webuiPort());
    connect(sc, &SettingsController::webuiEnabledChanged, this,
            [this, webui]() { webui->setEnabled(m_settingsController->webuiEnabled()); });
    connect(sc, &SettingsController::webuiPortChanged, this,
            [this, webui]() { webui->setPort(m_settingsController->webuiPort()); });

    // 机器人控制通道也可下发安装任务
    m_botController->setInstallCoordinator(m_installCoordinator.get());

    // 远控端控制服务（独立于 WebUI）：设备配对 + 控制反代，随启动自动监听
    m_remoteControl->setExposeLan(sc->webuiExposeLan());
    m_remoteControl->start();
    connect(sc, &SettingsController::webuiExposeLanChanged, this, [this]() {
        m_remoteControl->setExposeLan(m_settingsController->webuiExposeLan());
    });
}
