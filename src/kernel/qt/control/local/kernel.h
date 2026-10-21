#pragma once

// ============================================================================
// Kernel / Control / 组合根（Kernel）
// ----------------------------------------------------------------------------
// Kernel 是内核的唯一装配点：它按依赖顺序构造 Management / Execution / Control 三层全部服务，
// 完成它们之间的信号连线，并把每个服务以名字注册进 ModuleBus。
//
// 这样做的原因（历史教训）：
//   - 过去所有对象都堆在 main.cpp 里以栈对象构造、再手工 connect 约 40 条信号，
//     main.cpp 膨胀到 60KB 成为「上帝装配文件」，任何重构都要先读懂整个文件；
//   - 构造/析构顺序靠栈上的书写顺序隐式保证，稍有调整就会在退出时出现野指针崩溃；
//   - 模块之间直接互相 #include、互相持有裸指针，java <-> download 甚至形成了双向依赖。
//
// 现在的约定：
//   1. 成员声明顺序 == 构造顺序 == 依赖顺序；std::unique_ptr 按声明的逆序析构，
//      因此「被依赖者后析构」这一约束由语言规则保证，不再依赖人工记忆。
//   2. 内核服务对象只能经 Kernel 的具名 getter 或 ModuleBus::service(name) 取得，
//      模块之间不得再互相 #include 对方的内部头文件。
//   3. 接口层（GUI / CLI / TLI / WebUI / 机器人）一律通过 Control 层访问内核，
//      不直接触碰执行模块。
// ============================================================================

#include <QObject>
#include <QString>
#include <memory>

#include "modulebus.h"
#include "diskaccess.h"
#include "processprivilege.h"
#include "resourceallocator.h"

// ---- Execution ----
#include "downloadmanager.h"
#include "downloadcatalog.h"
#include "javamanager.h"
#include "servermanager.h"
#include "servercontroller.h"
#include "systemmonitor.h"
#include "createservercontroller.h"
#include "modpackimporter.h"
#include "installcoordinator.h"
#include "settingscontroller.h"
#include "notifier.h"
#include "backupcontroller.h"
#include "schedulercontroller.h"
#include "updatecontroller.h"
#include "portmapper.h"
#include "proxycontroller.h"
#include "proxymanager.h"

// ---- Control / Remote ----
#include "webuiserver.h"
#include "remotecontrolserver.h"
#include "botcontroller.h"

#include "appearancesettings.h"

class Kernel : public QObject
{
    Q_OBJECT
public:
    explicit Kernel(QObject *parent = nullptr);
    ~Kernel() override;

    // 启动：完成所有跨模块信号接线与受管目录初始化。必须在构造后、接口层接管前调用一次。
    void start();

    // 退出清理：强杀所有派生子进程（服务端 java、代理、NapCat/NoneBot），避免孤儿进程残留。
    Q_INVOKABLE void shutdown();

    // ---- Management ----
    ModuleBus *bus() const { return m_bus.get(); }
    DiskAccess *diskAccess() const { return m_diskAccess.get(); }
    ProcessPrivilege *processPrivilege() const { return m_processPrivilege.get(); }
    ResourceAllocator *resourceAllocator() const { return m_resourceAllocator.get(); }

    // ---- Execution ----
    ServerManager *serverManager() const { return m_serverManager.get(); }
    ServerController *serverController() const { return m_serverController.get(); }
    SystemMonitor *systemMonitor() const { return m_systemMonitor.get(); }
    DownloadManager *downloadManager() const { return m_downloadManager.get(); }
    DownloadCatalog *downloadCatalog() const { return m_downloadCatalog.get(); }
    JavaManager *javaManager() const { return m_javaManager.get(); }
    InstallCoordinator *installCoordinator() const { return m_installCoordinator.get(); }
    CreateServerController *createServer() const { return m_createServer.get(); }
    ModpackImporter *importModpack() const { return m_importModpack.get(); }
    SettingsController *settingsController() const { return m_settingsController.get(); }
    AppearanceSettings *appearance() const { return m_appearance.get(); }
    Notifier *notifier() const { return m_notifier.get(); }
    BackupController *backupController() const { return m_backupController.get(); }
    SchedulerController *schedulerController() const { return m_schedulerController.get(); }
    UpdateController *updateController() const { return m_updateController.get(); }
    PortMapper *portMapper() const { return m_portMapper.get(); }
    ProxyManager *proxyManager() const { return m_proxyManager.get(); }
    ProxyController *proxyController() const { return m_proxyManager->defaultProxy(); }

    // ---- Control / Remote ----
    WebUIServer *webuiServer() const { return m_webuiServer.get(); }
    BotController *botController() const { return m_botController.get(); }
    RemoteControlServer *remoteControlServer() const { return m_remoteControl.get(); }

signals:
    void started();

private:
    void wireSettings();    // 设置变更 -> 各执行模块
    void wireEvents();      // 执行模块事件 -> 通知 / 机器人
    void wireControl();     // 控制层（WebUI / 远控 / 机器人）接线

    // 声明顺序 == 构造顺序；析构按逆序进行，保证被依赖者最后析构。
    // ---- Management ----
    std::unique_ptr<ModuleBus> m_bus;
    std::unique_ptr<DiskAccess> m_diskAccess;
    std::unique_ptr<ProcessPrivilege> m_processPrivilege;
    std::unique_ptr<ResourceAllocator> m_resourceAllocator;

    // ---- Execution ----
    std::unique_ptr<ServerManager> m_serverManager;
    std::unique_ptr<SystemMonitor> m_systemMonitor;
    std::unique_ptr<DownloadManager> m_downloadManager;
    std::unique_ptr<JavaManager> m_javaManager;              // 依赖 downloadManager
    std::unique_ptr<DownloadCatalog> m_downloadCatalog;      // 依赖 downloadManager + javaManager
    std::unique_ptr<InstallCoordinator> m_installCoordinator;
    std::unique_ptr<CreateServerController> m_createServer;
    std::unique_ptr<ModpackImporter> m_importModpack;
    std::unique_ptr<ServerController> m_serverController;
    std::unique_ptr<SettingsController> m_settingsController;
    std::unique_ptr<AppearanceSettings> m_appearance;
    std::unique_ptr<ProxyManager> m_proxyManager;
    std::unique_ptr<PortMapper> m_portMapper;
    std::unique_ptr<BackupController> m_backupController;
    std::unique_ptr<SchedulerController> m_schedulerController;  // 依赖 serverManager/serverController/backup
    std::unique_ptr<UpdateController> m_updateController;
    std::unique_ptr<Notifier> m_notifier;

    // ---- Control / Remote ----
    std::unique_ptr<WebUIServer> m_webuiServer;
    std::unique_ptr<BotController> m_botController;
    std::unique_ptr<RemoteControlServer> m_remoteControl;
};
