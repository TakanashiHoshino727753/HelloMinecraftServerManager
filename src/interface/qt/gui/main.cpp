// ============================================================================
// HMCSM 桌面端入口（Interface / Desktop GUI）
// ----------------------------------------------------------------------------
// 本文件只负责「进程级」的事情：日志与 TLS 后端初始化、QApplication、主题加载、
// 内核装配、QML 引擎与托盘控制器的装配，然后进入事件循环。
//
// 所有业务对象的构造与跨模块接线已下沉到 Kernel（src/kernel/control/local/kernel.cpp）；
// 所有窗口/托盘逻辑已下沉到 AppController（appcontroller.h）。本文件因此保持极薄，
// 不再出现任何具体业务对象（ServerController / DownloadManager / BotController …）。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QColor>
#include <QDebug>
#include <QDirIterator>
#include <QIcon>
#include <QLibraryInfo>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStyleFactory>
#include <QStringList>
#include <QtQuickControls2/QQuickStyle>

#include <cstdarg>
#include <cstdio>

// 统一日志处理器（桌面端/远控端共用）：所有级别写入 logs/hmcsm.log；
// Debug 构建额外回显控制台并写入 logs/hmcsm-debug.log。
#include "hmcsm_logging.h"

// 内核：Management / Execution / Control 三层的唯一装配点
#include "kernel.h"
#include "localconnection.h"

// 界面层：窗口 / 托盘 / 背景音乐 / 主题
#include "appcontroller.h"

// 二维码图片提供者（Control/Remote/Pairing）：QML 用 image://qr/<text> 渲染配对二维码
#include "qrimageprovider.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// 诊断模式（Debug 专属）：在 cmd/控制台打印 qrc 资源路径、QML 导入路径等信息后退出（不启动 GUI）。
// 用法：MinecraftServerManager.exe --console   （简写 -c / -diag）。仅 Debug 构建编译/可用。
#ifdef QT_DEBUG
static void hmcsmConsoleMsgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    fflush(stderr);
    hmcsmAppendLog(msg.toLocal8Bit());   // 诊断模式的 qDebug/qWarning 同样进 hmcsm.log
}
// 诊断模式专用：同时打印到控制台(stdout)并写入 logs/hmcsm.log
static void hmcsmDiagPrint(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    const QString s = QString::vasprintf(fmt, ap);
    va_end(ap);
    fprintf(stdout, "%s", qPrintable(s));
    fflush(stdout);
    hmcsmAppendLog(s.toLocal8Bit());
}

static int runConsoleDiagnostics(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 优先附加到父控制台（在已有 cmd 中运行）；失败则说明是双击启动，新建一个控制台窗口
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif
    qInstallMessageHandler(hmcsmConsoleMsgHandler);
    QCoreApplication app(argc, argv);

    hmcsmDiagPrint("=== Minecraft Server Manager 诊断模式 ===\n");
    hmcsmDiagPrint("Qt 版本 : %s\n", qVersion());
    hmcsmDiagPrint("程序目录 : %s\n", qPrintable(QCoreApplication::applicationDirPath()));
    hmcsmDiagPrint("\n--- QML 导入路径 ---\n");
    hmcsmDiagPrint("%s\n", qPrintable(QLibraryInfo::path(QLibraryInfo::Qml2ImportsPath)));

    hmcsmDiagPrint("\n--- 已注册 qrc 资源路径 ---\n");
    QStringList paths;
    QDirIterator it(QStringLiteral(":/"), QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); paths.append(it.filePath()); }
    paths.sort();
    for (const QString &p : paths)
        hmcsmDiagPrint("%s\n", qPrintable(p));
    hmcsmDiagPrint("\n共 %d 个 qrc 资源。\n", (int)paths.size());

    hmcsmDiagPrint("\n按 Enter 退出...");
    getchar();
    return 0;
}
#endif // QT_DEBUG

int main(int argc, char *argv[])
{
    hmcsmInstallLogging(QStringLiteral("hmcsm.log"), QStringLiteral("hmcsm-debug.log"));   // 统一日志：debug 额外回显控制台+hmcsm-debug.log
    // 使用 OpenSSL 作为 TLS 后端：部署目录已随包附带 libssl-3-x64.dll / libcrypto-3-x64.dll
    // （SChannel 后端无法可靠加载 PEM 私钥，而 WebUI 自签证书正是 openssl 生成的 PEM）。
    qputenv("QT_TLS_BACKEND", "openssl");

    // 锁定 Qt RHI 渲染后端为 OpenGL：避免依赖 Vulkan 头文件/SDK。
    qputenv("QSG_RHI_BACKEND", "opengl");

    // 诊断模式（Debug 专属）：传入 --console / -c / -diag 时打印诊断信息后退出。
#ifdef QT_DEBUG
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("--console") || a == QStringLiteral("-c") || a == QStringLiteral("-diag")) {
            return runConsoleDiagnostics(argc, argv);
        }
    }
#endif

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qDebug() << "[APP] aboutToQuit";
    });

    // 设置应用图标：让任务栏/运行窗口使用 HMCSM.ico
    app.setWindowIcon(QIcon(QStringLiteral(":/icon/ApplicationIcon")));

    // Qt 的 QNetworkAccessManager 默认不读取系统代理：处于需要代理才能访问外网的环境时，
    // 所有出站 HTTPS 直连会被防火墙重置（表现为"Connection Closed"/列表永远加载不出来）。
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    // 固定组织/应用名，使 QSettings 落在稳定位置（主题持久化）
    app.setOrganizationName(QStringLiteral("HMCSM"));
    app.setApplicationName(QStringLiteral("MinecraftServerManager"));

    // 加载持久化主题（默认深色 + 蓝主色），应用到全局调色板；
    // 外观模型的 appTag（皮肤目录）由 Kernel 构造时设置。
    QSettings settings;
    const bool darkTheme = settings.value(QStringLiteral("theme/dark"), true).toBool();
    const QColor accentColor = settings.value(QStringLiteral("theme/accent"),
                                              QColor("#4f8cff")).value<QColor>();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));   // 原生控件（托盘菜单）跟随调色板
    AppController::applyTheme(darkTheme, accentColor);                         // 应用持久化调色板（须在样式之后）
    QQuickStyle::setStyle(QStringLiteral("Basic"));                            // QML 控件样式（独立于原生控件）

    // ===== 内核装配：构造 Management / Execution / Control 三层并完成全部跨模块接线 =====
    Kernel kernel;
    kernel.start();

    // ===== QML 引擎 =====
    // 必须在内核之后创建：退出时引擎最先析构，先销毁 QML 对象树，此时上下文属性对象
    //（内核服务）尚未析构，绑定求值访问到的仍是有效对象，
    // 避免退出时大量 "Cannot read property 'xxx' of null" 报错。
    QQmlApplicationEngine engine;
    // 二维码图片提供者：QML 通过 image://qr/<encodedUri> 直接渲染配对二维码位图
    engine.addImageProvider(QStringLiteral("qr"), new QrImageProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // AppController 作为引擎子对象：引擎析构晚期才销毁，退出时 QML 仍可安全访问它。
    AppController *appController = new AppController(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), appController);

    // ===== 本地直连：把内核服务按名注册为 QML 上下文属性（名字与 ModuleBus 一致）=====
    LocalConnection local(&kernel);
    local.bindTo(engine.rootContext());
    engine.rootContext()->setContextProperty(QStringLiteral("kernel"), &kernel);
    engine.rootContext()->setContextProperty(QStringLiteral("themeDark"), darkTheme);
    engine.rootContext()->setContextProperty(QStringLiteral("themeAccent"), accentColor);

    // 退出前释放所有运行中的子进程，避免服务器/bot/代理进程残留
    appController->setQuitCleanup([&kernel]() { kernel.shutdown(); });

    appController->setSettingsController(kernel.settingsController());
    // 语言切换：QML 侧通过 I18n.t(I18n.lang) 绑定自动重算；
    // C++ 侧托盘菜单通过 AppController 的字典 + settingsController.language() 决定中/英。
    appController->rebuildTrayMenu();
    QObject::connect(kernel.settingsController(), &SettingsController::languageChanged,
                     appController, [&]() { appController->rebuildTrayMenu(); });

    // WebUI <-> 本地端 主题双向同步
    kernel.webuiServer()->setThemeState(darkTheme, accentColor);
    QObject::connect(kernel.webuiServer(), &WebUIServer::themeChangeRequested,
                     appController, &AppController::setTheme);
    QObject::connect(appController, &AppController::themeApplied,
                     kernel.webuiServer(), &WebUIServer::setThemeState);

    // 只用 Qt 自身安装位置解析出的 QML 导入目录；去掉原先硬编码的绝对路径
    //（该路径与机器不符会导致 QtQuick.Controls 加载失败、MainWindow 创建不出来）。
    engine.addImportPath(QLibraryInfo::path(QLibraryInfo::Qml2ImportsPath));

    // 启动窗口策略：
    // - 开启"启动时显示窗口"或没有托盘：创建主窗口并显示；
    //   有原生托盘时关闭窗口只是收起托盘，程序常驻。
    // - 无托盘（无通知区的环境）：显示主窗口，关闭即退出程序。
    if (kernel.settingsController()->showOnStartup() || !appController->hasTray())
        appController->showMainWindow();
    // 有托盘且未要求启动时显示窗口：仅驻留托盘，不显示主窗口。

    return app.exec();
}
