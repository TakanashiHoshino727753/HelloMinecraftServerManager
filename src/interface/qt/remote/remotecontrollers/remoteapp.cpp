// remoteapp.cpp —— 见 remoteapp.h
#include "remoteapp.h"
#include "remoteclient.h"
#include "appearancesettings.h"
#include <QCoreApplication>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QDebug>

RemoteAppController::RemoteAppController(RemoteClient *rc, AppearanceSettings *appearance,
                                         QObject *parent)
    : QObject(parent), m_rc(rc), m_app(appearance)
{
    if (m_app) {
        // 把 AppearanceSettings 的外观信号转发为本控制器同名信号（与桌面端 AppController 一致）
        connect(m_app, &AppearanceSettings::bgmEnabledChanged, this, &RemoteAppController::bgmEnabledChanged);
        connect(m_app, &AppearanceSettings::bgmVolumeChanged, this, &RemoteAppController::bgmVolumeChanged);
        connect(m_app, &AppearanceSettings::bgImagePathChanged, this, &RemoteAppController::bgImagePathChanged);
        connect(m_app, &AppearanceSettings::bgScrimOpacityChanged, this, &RemoteAppController::bgScrimOpacityChanged);
        connect(m_app, &AppearanceSettings::uiTransparencyChanged, this, &RemoteAppController::uiTransparencyChanged);
        connect(m_app, &AppearanceSettings::bgLayerOpacityChanged, this, &RemoteAppController::bgLayerOpacityChanged);
    }
}

QString RemoteAppController::appVersion() const
{
    return QStringLiteral("远程控制端 / WebUI 转发");
}

bool RemoteAppController::bgmEnabled() const { return m_app ? m_app->bgmEnabled() : false; }
bool RemoteAppController::bgmAvailable() const { return true; }
double RemoteAppController::bgmVolume() const { return m_app ? m_app->bgmVolume() : 0.5; }
QString RemoteAppController::bgImageFullPath() const { return m_app ? m_app->bgImageFullPath() : QString(); }
bool RemoteAppController::bgImageVisible() const { return m_app ? m_app->bgImageVisible() : false; }
double RemoteAppController::bgScrimOpacity() const { return m_app ? m_app->bgScrimOpacity() : 0.45; }
double RemoteAppController::uiTransparency() const { return m_app ? m_app->uiTransparency() : 1.0; }
double RemoteAppController::bgLayerOpacity() const { return m_app ? m_app->bgLayerOpacity() : 1.0; }

bool RemoteAppController::hasTray() const { return QSystemTrayIcon::isSystemTrayAvailable(); }

void RemoteAppController::setBgmEnabled(bool v) { if (m_app) m_app->setBgmEnabled(v); }
void RemoteAppController::setBgmVolume(double v) { if (m_app) m_app->setBgmVolume(v); }
void RemoteAppController::setBgScrimOpacity(double v) { if (m_app) m_app->setBgScrimOpacity(v); }
void RemoteAppController::setUiTransparency(double v) { if (m_app) m_app->setUiTransparency(v); }
void RemoteAppController::setBgLayerOpacity(double v) { if (m_app) m_app->setBgLayerOpacity(v); }

void RemoteAppController::setTheme(bool dark, const QColor &accent)
{
    QSettings s;
    s.setValue(QStringLiteral("ui/darkTheme"), dark);
    s.setValue(QStringLiteral("ui/accentColor"), accent.name(QColor::HexArgb));
    if (m_engine) {
        // Theme 是 pragma 单例，首次实例化后 setContextProperty 不会让其绑定重算，
        // 因此直接写入单例实例属性（与桌面端 AppController::setTheme 一致）。
        m_engine->rootContext()->setContextProperty(QStringLiteral("themeDark"), dark);
        m_engine->rootContext()->setContextProperty(QStringLiteral("themeAccent"), accent);
        if (QObject *theme = m_engine->singletonInstance<QObject *>("MinecraftServerManager", "Theme")) {
            theme->setProperty("dark", dark);
            theme->setProperty("accent", QVariant::fromValue(accent));
        }
    }
    emit themeApplied(dark, accent);
}

void RemoteAppController::setEngine(QQmlApplicationEngine *engine) { m_engine = engine; }

QQuickWindow *RemoteAppController::createWindow(const QString &type)
{
    if (!m_engine)
        return nullptr;
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
    m_windows.append(win);
#ifdef Q_OS_WIN
    // 让无边框窗口支持"单击任务栏图标最小化"（标准 Windows 行为）。
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_APPWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif
    return win;
}

QQuickWindow *RemoteAppController::showUnique(const QString &type)
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

void RemoteAppController::showMainWindow() { qDebug() << "[remote] appController.showMainWindow"; }
void RemoteAppController::openDownloadCenter() { showUnique(QStringLiteral("DownloadCenter")); }
void RemoteAppController::openControllerSettings() { showUnique(QStringLiteral("ControllerSettings")); }
void RemoteAppController::openCreateServer() {
    QQuickWindow *w = showUnique(QStringLiteral("CreateServerDialog"));
    if (w) QMetaObject::invokeMethod(w, "resetAndOpen");
}
void RemoteAppController::openImportModpack() {
    QQuickWindow *w = showUnique(QStringLiteral("ImportModpackDialog"));
    if (w) QMetaObject::invokeMethod(w, "resetAndOpen");
}
void RemoteAppController::closeAllWindows() {
    QList<QQuickWindow *> list = m_windows;
    for (QQuickWindow *w : list) {
        if (w)
            w->close();
    }
}

void RemoteAppController::quitApp()
{
    // 远控端无本地子进程（服务器/bot/代理都跑在本地端），直接退出
    QCoreApplication::quit();
}
