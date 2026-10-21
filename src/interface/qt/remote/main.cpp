// HMCSM_Remote main.cpp —— 远控端入口。本地端的远程版：UI 与本地端一致，操作经 HTTPS REST 发给本地端执行。
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QTranslator>
#include <QtQml>
#include <QQmlComponent>
#include <QColor>
#include <QQuickStyle>
#include <QMediaPlayer>
#include <QAudioOutput>
#include "remoteclient.h"
#include "appearancesettings.h"
#include "hmcsm_logging.h"   // 统一日志处理器（desktop/remote 共用）
#include "remotecontrollers/remoteserver.h"
#include "remotecontrollers/remotesystem.h"
#include "remotecontrollers/remoteapp.h"
#include "remotecontrollers/remotewebui.h"
#include "remotecontrollers/remoteproxycontroller.h"
#include "remotecontrollers/remoteproxymanager.h"
#include "remotecontrollers/remotedownload.h"
#include "remotecontrollers/remoteinstall.h"
#include "remotecontrollers/remotesettings.h"
#include "remotecontrollers/remotemaintenance.h"

int main(int argc, char *argv[])
{
    // 统一日志：所有级别写入 logs/hmcsm-remote.log；Debug 构建额外回显控制台并写 logs/hmcsm-remote-debug.log
    hmcsmInstallLogging(QStringLiteral("hmcsm-remote.log"), QStringLiteral("hmcsm-remote-debug.log"));

    // 必须用 openssl 后端（MinGW 下 schannel 偶发 device not open）；本地端自签证书
    qputenv("QT_TLS_BACKEND", "openssl");

    QApplication app(argc, argv);
    // 与桌面端一致：QML 控件使用 Basic 样式。桌面端 UI 大量自定义 control 的 background，
    // 原生样式会拒绝此类定制并告警；Basic 样式支持，且视觉与桌面端统一。
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setOrganizationName("HMCSM");
    app.setApplicationName("HMCSM_Remote");
    // 共享外观模型的皮肤目录按应用区分（远控端 tag=HMCSM_Remote，与旧版路径完全一致，无需迁移）
    AppearanceSettings::setAppTag(QStringLiteral("HMCSM_Remote"));
    app.setWindowIcon(QIcon(":/icons/hmcsm_remote.svg"));

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    QQmlApplicationEngine engine;

    // 远控端核心：连接本地端控制器并转发操作
    RemoteClient remote;
    engine.rootContext()->setContextProperty("remote", &remote);
    remote.tryAutoConnect();   // 已配对则自动重连（deviceToken 抗本地端访问令牌轮换）

    // 服务器域控制器（REST 转发版，接口与桌面端 serverManager/serverController 严格一致）
    RemoteServerManager serverManager(&remote);
    RemoteServerController serverController(&remote);
    engine.rootContext()->setContextProperty("serverManager", &serverManager);
    engine.rootContext()->setContextProperty("serverController", &serverController);

    // 外观个性化（背景图/背景音乐/标题栏/壁纸轮换/透明度）：与桌面端共用同一实现
    AppearanceSettings appearance;
    engine.rootContext()->setContextProperty("appearance", &appearance);
    engine.rootContext()->setContextProperty("isRemoteEnd", true);   // 共用 QML 据此区分“配对(主机)/连接(客户端)”段

    // 系统监控（REST 转发版，镜像桌面端 systemMonitor）
    RemoteSystemMonitor systemMonitor(&remote);
    engine.rootContext()->setContextProperty("systemMonitor", &systemMonitor);

    // 应用控制器（镜像桌面端 appController：退出/主题为本地行为，外观委托 AppearanceSettings）
    RemoteAppController appController(&remote, &appearance);
    engine.rootContext()->setContextProperty("appController", &appController);

    // 代理管理器（镜像桌面端 proxyManager：多代理实例列表，REST 转发版）
    RemoteProxyManager proxyManager(&remote);
    engine.rootContext()->setContextProperty("proxyManager", &proxyManager);

    // 下载 / Java（镜像 desktop DownloadManager / DownloadCatalog / JavaManager）
    RemoteDownloadManager downloadManager(&remote);
    engine.rootContext()->setContextProperty("downloadManager", &downloadManager);
    RemoteDownloadCatalog downloadCatalog(&remote);
    engine.rootContext()->setContextProperty("downloadCatalog", &downloadCatalog);
    // 本机下载管理器：下载目标=本机时直接拉取模组源落在本机（与“下载到远端”互斥）
    RemoteLocalDownloader localDownloader;
    downloadCatalog.setLocalDownloader(&localDownloader);
    downloadCatalog.setTarget(QSettings().value(QStringLiteral("remote/downloadTarget"),
                                                 QStringLiteral("remote")).toString());
    engine.rootContext()->setContextProperty("localDownloader", &localDownloader);
    RemoteJavaManager javaManager(&remote);
    engine.rootContext()->setContextProperty("javaManager", &javaManager);

    // 安装 / 创建 / 导入（镜像 desktop InstallCoordinator / CreateServerController / ModpackImporter）
    RemoteInstallCoordinator installCoordinator(&remote);
    engine.rootContext()->setContextProperty("installCoordinator", &installCoordinator);
    RemoteCreateServer createServer(&remote);
    engine.rootContext()->setContextProperty("createServer", &createServer);
    RemoteImportModpack importModpack(&remote);
    engine.rootContext()->setContextProperty("importModpack", &importModpack);

    // 设置 / Bot（镜像 desktop SettingsController / BotController）
    RemoteSettingsController settingsController(&remote);
    settingsController.setAppearance(&appearance);
    engine.rootContext()->setContextProperty("settingsController", &settingsController);
    RemoteBotController botController(&remote);
    engine.rootContext()->setContextProperty("botController", &botController);

    // WebUI 服务（镜像 desktop WebuiServer；远控端不运行 WebUI，仅转发状态/配对，UI 优雅降级）
    RemoteWebuiServer webuiServer(&remote);
    engine.rootContext()->setContextProperty("webuiServer", &webuiServer);

    // 运维（镜像 desktop PortMapper / BackupController / SchedulerController / UpdateController）
    RemotePortMapper portMapper(&remote);
    engine.rootContext()->setContextProperty("portMapper", &portMapper);
    RemoteBackupController backupController(&remote);
    engine.rootContext()->setContextProperty("backupController", &backupController);
    RemoteSchedulerController schedulerController(&remote);
    engine.rootContext()->setContextProperty("schedulerController", &schedulerController);
    RemoteUpdateController updateController(&remote);
    engine.rootContext()->setContextProperty("updateController", &updateController);

    // 背景音乐播放：资源为用户选文件复制到 backgroundmusic/bgm.<ext>（回退 qrc:/audio/bgm）
    {
        QMediaPlayer *bgm = new QMediaPlayer(&app);
        bgm->setAudioOutput(new QAudioOutput(&app));
        bgm->setLoops(QMediaPlayer::Infinite);
        const QString skinBgm = AppearanceSettings::bgmDir() + QStringLiteral("/") + appearance.bgmPath();
        if (!appearance.bgmPath().isEmpty() && QFile::exists(skinBgm))
            bgm->setSource(QUrl::fromLocalFile(skinBgm));
        else if (QFile::exists(QStringLiteral(":/audio/bgm.mp3")))
            bgm->setSource(QUrl(QStringLiteral("qrc:/audio/bgm.mp3")));
        if (bgm->audioOutput())
            bgm->audioOutput()->setVolume(appearance.bgmVolume());
        if (appearance.bgmEnabled() && !bgm->source().isEmpty())
            bgm->play();
        // 开关/音量变化实时联动
        QObject::connect(&appearance, &AppearanceSettings::bgmEnabledChanged, &app, [bgm, &appearance]() {
            if (appearance.bgmEnabled() && !bgm->source().isEmpty())
                bgm->play();
            else
                bgm->stop();
        });
        QObject::connect(&appearance, &AppearanceSettings::bgmVolumeChanged, &app, [bgm, &appearance]() {
            if (bgm->audioOutput())
                bgm->audioOutput()->setVolume(appearance.bgmVolume());
        });
        QObject::connect(&appearance, &AppearanceSettings::bgmPathChanged, &app, [bgm, &appearance]() {
            const QString p = appearance.bgmPath().isEmpty() ? QString()
                : (AppearanceSettings::bgmDir() + QStringLiteral("/") + appearance.bgmPath());
            if (!p.isEmpty() && QFile::exists(p)) {
                bgm->setSource(QUrl::fromLocalFile(p));
                if (appearance.bgmEnabled())
                    bgm->play();
            }
        });
    }

    // 简易暗色主题（与原 HMCSM 风格统一）
    // 主题：从持久化设置下发 themeDark/themeAccent 上下文属性（桌面端 Theme.qml 据此初始化）
    QSettings appSettings;
    const bool darkTheme = appSettings.value(QStringLiteral("ui/darkTheme"), true).toBool();
    const QColor accentColor = QColor(appSettings.value(QStringLiteral("ui/accentColor"),
                                                       QStringLiteral("#4f8cff")).toString());
    engine.rootContext()->setContextProperty(QStringLiteral("themeDark"), darkTheme);
    engine.rootContext()->setContextProperty(QStringLiteral("themeAccent"), accentColor);

    // 让 RemoteAppController 能打开子窗口（DownloadCenter / CreateServerDialog / ImportModpackDialog / ControllerSettings）
    appController.setEngine(&engine);

    // 照搬桌面端：从 MinecraftServerManager 模块加载 MainWindow 主窗口
    QQmlComponent mainComp(&engine);
    mainComp.loadFromModule(QStringLiteral("MinecraftServerManager"), QStringLiteral("MainWindow"));
    if (mainComp.isError()) {
        qWarning() << "加载 MainWindow 失败:" << mainComp.errors();
        return -1;
    }
    QObject *mainObj = mainComp.create(engine.rootContext());
    auto *mainWin = qobject_cast<QQuickWindow *>(mainObj);
    if (!mainWin) {
        qWarning() << "MainWindow 不是 Window 类型";
        delete mainObj;
        return -1;
    }

    // 系统托盘（常驻；默认无窗口，仅驻留托盘；双击或菜单“显示窗口”再弹出；关窗=收起托盘）
    QSystemTrayIcon *tray = nullptr;
    QMenu *trayMenu = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(&app);
        tray->setIcon(QIcon(QStringLiteral(":/icons/hmcsm_remote.svg")));
        tray->setToolTip(QStringLiteral("HMCSM 远控端"));

        trayMenu = new QMenu;
        QAction *showAct = trayMenu->addAction(QStringLiteral("显示窗口"));
        QAction *quitAct = trayMenu->addAction(QStringLiteral("退出"));
        QObject::connect(showAct, &QAction::triggered, &app, [mainWin]() {
            if (mainWin) { mainWin->show(); mainWin->raise(); mainWin->requestActivate(); }
        });
        QObject::connect(quitAct, &QAction::triggered, &app, &QApplication::quit);
        QObject::connect(tray, &QSystemTrayIcon::activated, &app,
                         [mainWin](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::DoubleClick && mainWin) {
                                 mainWin->show(); mainWin->raise(); mainWin->requestActivate();
                             }
                         });
        tray->setContextMenu(trayMenu);
        tray->show();
        mainWin->hide();   // 有托盘时默认隐藏主窗口（仅驻留托盘）
    } else {
        app.setQuitOnLastWindowClosed(true);
        mainWin->show();
    }

    // 配对 / 连接对话框：未连接时弹出，要求先配对才能传输数据；连接成功后自动隐藏并展示主窗口
    QQmlComponent linkComp(&engine);
    linkComp.loadFromModule(QStringLiteral("MinecraftServerManager"), QStringLiteral("RemoteLinkDialog"));
    QQuickWindow *linkWin = nullptr;
    if (!linkComp.isError()) {
        QObject *linkObj = linkComp.create(engine.rootContext());
        linkWin = qobject_cast<QQuickWindow *>(linkObj);
    } else {
        qWarning() << "加载 RemoteLinkDialog 失败:" << linkComp.errors();
    }
    auto updateLinkWin = [&]() {
        if (!linkWin) return;
        if (remote.connected()) {
            linkWin->hide();
            if (mainWin) { mainWin->show(); mainWin->raise(); mainWin->requestActivate(); }
        } else {
            linkWin->show();
        }
    };
    QObject::connect(&remote, &RemoteClient::connectedChanged, &app, updateLinkWin);
    updateLinkWin();

    return app.exec();
}
