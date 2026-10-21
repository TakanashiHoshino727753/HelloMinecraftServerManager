#pragma once

// ============================================================================
// Interface / GUI / 窗口与托盘控制器（AppController）
// ----------------------------------------------------------------------------
// 桌面端界面层唯一的过程性入口：主窗口 / 独立窗口的创建与显隐、系统托盘与托盘菜单、
// 背景音乐播放、主题应用与翻译字典加载。
//
// 架构约定（与重构前一致，勿再回退）：QML 只负责画界面，所有程序逻辑都在 C++。
// 本类原先内联在 main.cpp 中，随架构调整下沉到 Interface/GUI 层——它只依赖
// QQmlApplicationEngine 与 SettingsController，不参与任何内核业务逻辑。
// ============================================================================

#include <QApplication>
#include <QObject>
#include <QColor>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "settingscontroller.h"
class AppController : public QObject
{
    Q_OBJECT
public:
    // 根据深浅色 + 主色调构建并应用全局调色板（原生控件随之变色），并写入 QSettings 持久化。
    // 声明为静态：界面层与入口都需要它（启动时应用持久化主题、设置页/WebUI 切换主题）。
    static void applyTheme(bool dark, const QColor &accent);

    explicit AppController(QQmlApplicationEngine *engine, QObject *parent = nullptr)
        : QObject(parent), m_engine(engine)
    {
        loadDict();  // 先加载翻译字典，供 rebuildTrayMenu 使用
        setupBgm();
        setupTray();
        // 壁纸轮换已交由共享的 AppearanceSettings 定时器统一负责（两端共用），此处不再自建。
    }

    // 初始化 BGM 播放器：资源缺失时优雅降级（bgmAvailable=false，开关无效）。
    // 资源优先级：用户在设置里从电脑选中的文件（复制到 backgroundmusic/bgm.<ext>）>
    // exe 同目录 bgm.mp3 > qrc:/audio/bgm.mp3。这样用户无需把文件丢进程序目录。
    void setupBgm()
    {
        m_bgm = new QMediaPlayer(this);
        m_bgm->setAudioOutput(new QAudioOutput(this));
        m_bgm->setLoops(QMediaPlayer::Infinite);
        // m_sc 可能尚未设置（构造期早于 setSettingsController），用 QSettings 兜底读取 bgm 路径
        const QString bgmPath = m_sc ? m_sc->bgmPath()
                                     : QSettings().value(QStringLiteral("app/bgmPath")).toString();
        const QString skinBgm = SettingsController::bgmDir() + QLatin1Char('/') + bgmPath;
        const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/bgm.mp3");
        QString src;
        if (!bgmPath.isEmpty() && QFile::exists(skinBgm)) {
            src = skinBgm;
        } else if (QFile::exists(local)) {
            src = local;
        } else if (QFile::exists(QStringLiteral(":/audio/bgm.mp3"))) {
            src = QStringLiteral("qrc:/audio/bgm.mp3");
        }
        if (!src.isEmpty()) {
            m_bgm->setSource(QUrl::fromLocalFile(src));
            m_bgmAvailable = true;
        } else {
            m_bgmAvailable = false;
        }
        // 开关与音量统一从 SettingsController 读取（app/bgmEnabled、app/bgmVolume）
        if (m_sc) {
            m_bgmEnabled = m_sc->bgmEnabled();
            if (m_bgm->audioOutput())
                m_bgm->audioOutput()->setVolume(m_sc->bgmVolume());
        } else {
            QSettings s;
            m_bgmEnabled = s.value(QStringLiteral("ui/bgm"), false).toBool();
        }
        if (m_bgmEnabled && m_bgmAvailable)
            m_bgm->play();
    }

    private:
    void setupTray()
    {
        // 所有平台统一使用系统原生托盘（QSystemTrayIcon）：图标落到通知区。
        // 仅当通知区可用时程序常驻（关闭窗口=收起托盘而非退出）；
        // 通知区不可用时（如无桌面的服务器环境）保持默认行为——关闭窗口即退出，避免程序空跑。
        // 托盘右键菜单（原生托盘使用），按需重建以反映各窗口可见状态
        m_trayMenu = new QMenu;
        connect(m_trayMenu, &QMenu::aboutToShow, this, &AppController::rebuildTrayMenu);
        // 菜单真正隐藏时（其模态嵌套事件循环已退出、控制权回到主循环）再执行退出，
        // 规避“直接 quit 只退出了菜单循环、主程序不退出”的问题。
        connect(m_trayMenu, &QMenu::aboutToHide, this, [this]() {
            if (m_quitRequested) {
                m_quitRequested = false;
                QApplication::quit();
            }
        });

        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            QApplication::setQuitOnLastWindowClosed(false);
            m_tray = new QSystemTrayIcon(this);
            m_tray->setIcon(QIcon(QStringLiteral(":/icon/ApplicationIcon")));
            m_tray->setToolTip(QStringLiteral("Minecraft Server Manager"));
            m_tray->setContextMenu(m_trayMenu);

            // 单击托盘图标：切换主窗口显隐（QQ 式行为）
            connect(m_tray, &QSystemTrayIcon::activated, this,
                    [this](QSystemTrayIcon::ActivationReason reason) {
                        if (reason == QSystemTrayIcon::Trigger)
                            toggleWindow(QStringLiteral("MainWindow"));
                    });

            rebuildTrayMenu();
            m_tray->show();
            m_hasTray = true;
        } else {
            qWarning("系统托盘（通知区）不可用：窗口关闭将直接退出程序"
                     "（如需常驻请安装带通知区的桌面环境，或在桌面环境中启用通知区）");
        }
    }

    QQuickWindow *createWindow(const QString &type)
    {
        QQmlComponent comp(m_engine);
        comp.loadFromModule(QStringLiteral("MinecraftServerManager"), type);
        if (comp.isError()) {
            qWarning() << "加载 QML 类型失败:" << type << comp.errors();
            return nullptr;
        }
        QObject *obj = comp.create(m_engine->rootContext());
        auto *win = qobject_cast<QQuickWindow *>(obj);
        if (!win) {
            qWarning() << type << "不是 Window 类型";
            delete obj;
            return nullptr;
        }

        win->installEventFilter(this);
        m_windows.append(win);
#ifdef Q_OS_WIN
        // 让无边框窗口支持"单击任务栏图标最小化"（标准 Windows 行为）。
        // Qt::FramelessWindowHint 创建的是 WS_POPUP 窗口，Windows 不会自动
        // 处理任务栏点击。切到 WS_OVERLAPPED、补 WS_SYSMENU+WS_MINIMIZEBOX
        // 和 WS_EX_APPWINDOW，Windows 才能正确响应任务栏单击（显隐切换）。
        // 不加 WS_CAPTION / WS_THICKFRAME，因此仍是无边框外观。
        HWND hwnd = reinterpret_cast<HWND>(win->winId());
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_APPWINDOW;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
        // 样式变更后必须 SetWindowPos + SWP_FRAMECHANGED，Windows 才会重算非客户区
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif
        return win;
    }

    QQuickWindow *showUnique(const QString &type)
    {
        QQuickWindow *w = m_unique.value(type, nullptr);
        if (!w) {
            w = createWindow(type);
            if (w) {
                m_unique.insert(type, w);
                QObject::connect(w, &QObject::destroyed, this,
                                 [this, type]() { m_unique.remove(type); });
            }
        }
        if (w) {
            w->show();
            w->raise();
            w->requestActivate();
        }
        return w;
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Close) {
            auto *win = qobject_cast<QQuickWindow *>(watched);
            if (win) {
                const QString mode = win->property("closeMode").toString();
                if (mode == QStringLiteral("close")) {
                    // 显式"关闭"模式：真正退出该窗口
                    m_windows.removeAll(win);
                } else if (m_tray) {
                    // 原生托盘：收起托盘（隐藏窗口，无任务栏条目；由托盘恢复）
                    event->ignore();
                    win->hide();
                    return true;
                } else {
                    m_windows.removeAll(win);
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

signals:
    void themeApplied(bool dark, const QColor &accent);
    void bgmEnabledChanged();
    void bgmVolumeChanged();
    void bgImagePathChanged();
    void bgScrimOpacityChanged();
    void uiTransparencyChanged();
    void bgLayerOpacityChanged();

public slots:
    void showMainWindow()            { showUnique(QStringLiteral("MainWindow")); }
    // 是否有原生托盘（通知区）可用
    bool hasTray() const { return m_hasTray; }
    // 切换指定窗口：已可见则收起（真实托盘隐藏 / 模拟托盘最小化到任务栏），
    // 已最小化则恢复，否则显示并置前（供托盘单击/菜单分别控制各窗口）
    void toggleWindow(const QString &type)
    {
        QQuickWindow *w = m_unique.value(type, nullptr);
        if (!w) {
            showUnique(type);
            return;
        }
        if (w->windowState() & Qt::WindowMinimized) {
            // 已最小化（模拟托盘场景）：恢复并置前
            w->showNormal();
            w->raise();
            w->requestActivate();
        } else if (w->isVisible()) {
            // 有托盘则收起隐藏；无托盘直接隐藏（窗口关闭即退出）
            w->hide();
        } else {
            showUnique(type);
        }
    }
    void openDownloadCenter()        { showUnique(QStringLiteral("DownloadCenter")); }
    void openCreateServer() {
        QQuickWindow *w = showUnique(QStringLiteral("CreateServerDialog"));
        if (w) QMetaObject::invokeMethod(w, "resetAndOpen");
    }
    void openImportModpack() {
        QQuickWindow *w = showUnique(QStringLiteral("ImportModpackDialog"));
        if (w) QMetaObject::invokeMethod(w, "resetAndOpen");
    }
    void openControllerSettings()    { showUnique(QStringLiteral("ControllerSettings")); }
    void setTheme(bool dark, const QColor &accent);   // 持久化并应用主题（QML 调用）
    // 壁纸轮换由共享 AppearanceSettings 统一负责（桌面端与远控端同一实现）。

public:
    // 应用版本号（标题栏副标题展示）
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    QString appVersion() const;

    // 背景音乐（BGM）开关/音量：资源为用户在设置里从电脑选中的文件（复制到 skinDir/bgm），
    // 回退到 exe 同目录 bgm.mp3 或 qrc:/audio/bgm.mp3。启用且资源存在时循环播放。
    Q_PROPERTY(bool bgmEnabled READ bgmEnabled NOTIFY bgmEnabledChanged)
    Q_PROPERTY(bool bgmAvailable READ bgmAvailable CONSTANT)
    Q_PROPERTY(double bgmVolume READ bgmVolume WRITE setBgmVolume NOTIFY bgmVolumeChanged)
    bool bgmEnabled() const;
    bool bgmAvailable() const;
    double bgmVolume() const;
    Q_INVOKABLE void setBgmEnabled(bool on);
    Q_INVOKABLE void setBgmVolume(double v);
    // 背景图在背景图目录下的绝对路径，供 QML 直接绑定（空字符串表示未设置）。
    // NOTIFY bgImagePathChanged 让选择新图后 MainWindow 的背景层自动刷新。
    Q_PROPERTY(QString bgImageFullPath READ bgImageFullPath NOTIFY bgImagePathChanged)
    QString bgImageFullPath() const;
    // 背景图是否可用（已启用且路径非空）——QML 据此显示背景层与压暗遮罩。
    Q_PROPERTY(bool bgImageVisible READ bgImageVisible NOTIFY bgImagePathChanged)
    bool bgImageVisible() const;
    // 背景图压暗强度（0~1），越大图片越暗、控件越清晰；用户可在设置里调节，持久化。
    Q_PROPERTY(double bgScrimOpacity READ bgScrimOpacity WRITE setBgScrimOpacity NOTIFY bgScrimOpacityChanged)
    double bgScrimOpacity() const;
    Q_INVOKABLE void setBgScrimOpacity(double v);
    // 界面透明度（0~1）：统一控制所有半透明控件（侧边栏背景、主区域容器、卡片、页签条等）
    // 从各自最小值到不透明的等比例映射。1=完全不透明，0=各自最小值（最透）。持久化。
    Q_PROPERTY(double uiTransparency READ uiTransparency WRITE setUiTransparency NOTIFY uiTransparencyChanged)
    double uiTransparency() const;
    Q_INVOKABLE void setUiTransparency(double v);
    // 区域底色透明度（0~1）：控制主区域底色与侧边栏底色。主区域从 0（全透）到 1；
    // 侧边栏从 0.2（起始）等比例到 1，始终为主区域的同斜率偏移，二者等比联动。持久化。
    Q_PROPERTY(double bgLayerOpacity READ bgLayerOpacity WRITE setBgLayerOpacity NOTIFY bgLayerOpacityChanged)
    double bgLayerOpacity() const;
    Q_INVOKABLE void setBgLayerOpacity(double v);


    void closeAllWindows()
    {
        QList<QQuickWindow *> list = m_windows;
        for (QQuickWindow *win : list) {
            if (!win)
                continue;
            const QString mode = win->property("closeMode").toString();
            if (mode == QStringLiteral("close"))
                win->close();          // 真正关闭
            else if (m_tray)
                win->hide();           // 原生托盘：收起托盘
            else
                win->close();          // 无托盘：关闭窗口（默认 quitOnLastWindowClosed 退出程序）
        }
    }

    void quitApp()
    {
        // 托盘菜单是模态的嵌套事件循环（exec/popup）。若直接在此调 QApplication::quit()，
        // 只会终止“菜单”这一层嵌套循环，最外层 app.exec() 主循环仍在运行——表现就是托盘
        // 图标消失但主程序进程不退出。正确做法：标记退出请求，关闭菜单；菜单 aboutToHide
        // 在其嵌套循环已退出、控制权回到主循环后才发出，那时再 QApplication::quit() 即退出主循环。
        // 另加一个较长延时（>菜单关闭耗时）的兜底 quit，确保即便 aboutToHide 未触发也能退出主循环。
        m_quitRequested = true;
        if (m_trayMenu && m_trayMenu->isVisible())
            m_trayMenu->close();
        else
            doQuit();
        if (m_tray)
            m_tray->hide();
        QTimer::singleShot(500, qApp, [this]() { doQuit(); });
    }

    // 注册退出前的清理回调（释放运行中的子进程等），由 main() 注入；
    // 仅在真正执行退出时调用一次，避免重复触发。
    void setQuitCleanup(std::function<void()> f) { m_cleanup = std::move(f); }

    void doQuit()
    {
        if (m_quitting)
            return;
        m_quitting = true;
        if (m_cleanup)
            m_cleanup();   // 释放所有运行中的子进程（服务器/bot/代理…），避免退出后残留
        QApplication::quit();
    }

private:
    QQmlApplicationEngine *m_engine = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    bool m_quitRequested = false;     // 托盘菜单“退出”已点击，待菜单关闭后真正退出主循环
    bool m_quitting = false;          // 真正退出流程已触发，防止清理回调/quit 重入
    bool m_hasTray = false;           // 是否已有原生托盘（通知区）可用
    std::function<void()> m_cleanup;  // 退出前清理回调（释放子进程）
    QHash<QString, QQuickWindow *> m_unique;
    QList<QQuickWindow *> m_windows;
    QHash<QString, QString> m_dict;
    SettingsController *m_sc = nullptr;
    QMediaPlayer *m_bgm = nullptr;
    bool m_bgmEnabled = false;
    bool m_bgmAvailable = false;

public:
    void setSettingsController(SettingsController *sc) {
        m_sc = sc;
        // m_sc 就绪后再建立转发：背景图路径/开关变化 → 通知 QML 刷新背景层
        // （不能放构造函数，那时 m_sc 还是 nullptr，connect 不会建立 → 选图/开关不实时刷新，需重启才生效）
        if (m_sc) {
            connect(m_sc, &SettingsController::bgImagePathChanged,
                    this, &AppController::bgImagePathChanged);
            connect(m_sc, &SettingsController::bgEnabledChanged,
                    this, &AppController::bgImagePathChanged);
            // 播放列表/当前索引变化也要通知背景层刷新（选文件夹/添加图片/切换当前项）；
            // 壁纸轮换本身由共享 AppearanceSettings 的定时器负责，这里只做 UI 通知。
            connect(m_sc, &SettingsController::bgImageListChanged,
                    this, &AppController::bgImagePathChanged);
            connect(m_sc, &SettingsController::bgImageIndexChanged,
                    this, &AppController::bgImagePathChanged);
            // BGM 状态联动：外观模型变化 → 播放器与 QML 通知（开关/音量/曲目）
            connect(m_sc, &SettingsController::bgmEnabledChanged, this, [this]() {
                m_bgmEnabled = m_sc->bgmEnabled();
                if (m_bgmEnabled && m_bgmAvailable && m_bgm)
                    m_bgm->play();
                else if (m_bgm)
                    m_bgm->stop();
                emit bgmEnabledChanged();
            });
            connect(m_sc, &SettingsController::bgmVolumeChanged, this, [this]() {
                if (m_bgm && m_bgm->audioOutput())
                    m_bgm->audioOutput()->setVolume(m_sc->bgmVolume());
                emit bgmVolumeChanged();
            });
            connect(m_sc, &SettingsController::bgmPathChanged, this, [this]() {
                const QString bgmPath = m_sc->bgmPath();
                const QString skinBgm = SettingsController::bgmDir() + QLatin1Char('/') + bgmPath;
                const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/bgm.mp3");
                QString src;
                if (!bgmPath.isEmpty() && QFile::exists(skinBgm))
                    src = skinBgm;
                else if (QFile::exists(local))
                    src = local;
                else if (QFile::exists(QStringLiteral(":/audio/bgm.mp3")))
                    src = QStringLiteral("qrc:/audio/bgm.mp3");
                if (!src.isEmpty()) {
                    m_bgm->setSource(QUrl::fromLocalFile(src));
                    m_bgmAvailable = true;
                } else {
                    m_bgmAvailable = false;
                }
                if (m_bgmEnabled && m_bgmAvailable)
                    m_bgm->play();
            });
        }
        // m_sc 就绪后按真实设置刷新 BGM 源/音量/开关（构造期 setupBgm 用的是 QSettings 兜底值）
        if (m_sc && m_bgm) {
            const QString bgmPath = m_sc->bgmPath();
            const QString skinBgm = SettingsController::bgmDir() + QLatin1Char('/') + bgmPath;
            const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/bgm.mp3");
            QString src;
            if (!bgmPath.isEmpty() && QFile::exists(skinBgm))
                src = skinBgm;
            else if (QFile::exists(local))
                src = local;
            else if (QFile::exists(QStringLiteral(":/audio/bgm.mp3")))
                src = QStringLiteral("qrc:/audio/bgm.mp3");
            if (!src.isEmpty()) {
                m_bgm->setSource(QUrl::fromLocalFile(src));
                m_bgmAvailable = true;
            } else {
                m_bgmAvailable = false;
            }
            m_bgmEnabled = m_sc->bgmEnabled();
            if (m_bgm->audioOutput())
                m_bgm->audioOutput()->setVolume(m_sc->bgmVolume());
            if (m_bgmEnabled && m_bgmAvailable)
                m_bgm->play();
        }
    }

private:
    void loadDict()
    {
        // tray 菜单只用这几条，硬编码避免资源路径问题
        m_dict = {
            {QStringLiteral("隐藏主窗口"), QStringLiteral("Hide Main Window")},
            {QStringLiteral("显示主窗口"), QStringLiteral("Show Main Window")},
            {QStringLiteral("下载中心"),     QStringLiteral("Downloads")},
            {QStringLiteral("创建服务器"),   QStringLiteral("Create Server")},
            {QStringLiteral("导入整合包"),   QStringLiteral("Import Modpack")},
            {QStringLiteral("控制器设置"),   QStringLiteral("Controller Settings")},
            {QStringLiteral("隐藏"),        QStringLiteral("Hide ")},
            {QStringLiteral("关闭所有窗口"), QStringLiteral("Close All Windows")},
            {QStringLiteral("退出"),        QStringLiteral("Exit")},
        };
    }

    bool isWindowVisible(const QString &type) const
    {
        QQuickWindow *w = m_unique.value(type, nullptr);
        return w && w->isVisible();
    }

public:
    // 销毁并重建全部 QML 窗口：让 QQmlComponent 在新的 uiLanguage 下重新创建窗口，
    // 新窗口中的 qsTr() 才会用新安装的翻译器重新求值。
    void recreateAllWindows()
    {
        const QList<QQuickWindow *> list = m_windows;
        m_unique.clear();
        m_windows.clear();
        for (QQuickWindow *w : list) {
            w->close();
            w->deleteLater();
        }
        // 清除 QML 内部组件缓存，否则 QQmlComponent::loadFromModule 仍使用之前缓存的编译结果，
        // qsTr 仍取不到新安装的翻译器。清完后再创建新窗口才会在新语言下重新编译 QML。
        m_engine->clearComponentCache();
        showMainWindow();
    }

    // 重建托盘右键菜单：主窗口单点切换项 + 各子窗口"打开/隐藏"切换 + 关闭全部/退出
    void rebuildTrayMenu()
    {
        QMenu *m = m_trayMenu;
        if (!m)
            return;
        m->clear();
        // 直接查表，不走 QCoreApplication::translate()（避免 Qt 版本差异导致不调用）
        auto T = [this](const QString &zh) -> QString {
            if (m_sc && m_sc->language() == QStringLiteral("English")) {
                auto it = m_dict.constFind(zh);
                return it != m_dict.constEnd() ? it.value() : zh;
            }
            return zh;  // 中文模式直接返回原文
        };
        QAction *a = m->addAction(isWindowVisible(QStringLiteral("MainWindow"))
                                  ? T(QStringLiteral("隐藏主窗口"))
                                  : T(QStringLiteral("显示主窗口")));
        connect(a, &QAction::triggered, this,
                [this]() { toggleWindow(QStringLiteral("MainWindow")); });
        m->addSeparator();

        // 多窗口分别控制：已打开则"隐藏"，未打开则"打开"
        const QVector<QPair<QString, QString>> entries = {
            { QStringLiteral("DownloadCenter"),      QStringLiteral("下载中心") },
            { QStringLiteral("CreateServerDialog"),  QStringLiteral("创建服务器") },
            { QStringLiteral("ImportModpackDialog"), QStringLiteral("导入整合包") },
            { QStringLiteral("ControllerSettings"),   QStringLiteral("控制器设置") },
        };
        for (const auto &e : entries) {
            const bool vis = isWindowVisible(e.first);
            const QString label = T(e.second);
            QAction *a2 = m->addAction(vis ? (T(QStringLiteral("隐藏")) + label) : label);
            connect(a2, &QAction::triggered, this,
                    [this, type = e.first]() { toggleWindow(type); });
        }
        m->addSeparator();
        QAction *hideAll = m->addAction(T(QStringLiteral("关闭所有窗口")));
        connect(hideAll, &QAction::triggered, this, &AppController::closeAllWindows);
        QAction *quit = m->addAction(T(QStringLiteral("退出")));
        connect(quit, &QAction::triggered, this, &AppController::quitApp);
    }
};
