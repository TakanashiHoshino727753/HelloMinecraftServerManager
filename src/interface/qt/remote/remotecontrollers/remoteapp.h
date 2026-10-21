// remoteapp.h —— 远控端对桌面端 AppController 的镜像
// 桌面端 AppController 负责：退出清理、主题/语言、背景音乐/壁纸、多窗口打开。
// 远控端：退出/主题/语言为本地行为；外观（BGM/壁纸/透明度）直接委托 AppearanceSettings；
// 多窗口打开器在远控端注册 MinecraftServerManager QML 模块后再行接上（本阶段先占位）。
#pragma once
#include <QObject>
#include <QColor>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

class RemoteClient;
class AppearanceSettings;

class RemoteAppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool bgmEnabled READ bgmEnabled NOTIFY bgmEnabledChanged)
    Q_PROPERTY(bool bgmAvailable READ bgmAvailable CONSTANT)
    Q_PROPERTY(double bgmVolume READ bgmVolume WRITE setBgmVolume NOTIFY bgmVolumeChanged)
    Q_PROPERTY(QString bgImageFullPath READ bgImageFullPath NOTIFY bgImagePathChanged)
    Q_PROPERTY(bool bgImageVisible READ bgImageVisible NOTIFY bgImagePathChanged)
    Q_PROPERTY(double bgScrimOpacity READ bgScrimOpacity WRITE setBgScrimOpacity NOTIFY bgScrimOpacityChanged)
    Q_PROPERTY(double uiTransparency READ uiTransparency WRITE setUiTransparency NOTIFY uiTransparencyChanged)
    Q_PROPERTY(double bgLayerOpacity READ bgLayerOpacity WRITE setBgLayerOpacity NOTIFY bgLayerOpacityChanged)
public:
    explicit RemoteAppController(RemoteClient *rc, AppearanceSettings *appearance,
                                 QObject *parent = nullptr);
    QString appVersion() const;
    bool bgmEnabled() const;
    bool bgmAvailable() const;
    double bgmVolume() const;
    QString bgImageFullPath() const;
    bool bgImageVisible() const;
    double bgScrimOpacity() const;
    double uiTransparency() const;
    double bgLayerOpacity() const;

    Q_INVOKABLE bool hasTray() const;
    Q_INVOKABLE void setBgmEnabled(bool v);
    Q_INVOKABLE void setBgmVolume(double v);
    Q_INVOKABLE void setBgScrimOpacity(double v);
    Q_INVOKABLE void setUiTransparency(double v);
    Q_INVOKABLE void setBgLayerOpacity(double v);
    // 主题：持久化到 QSettings，并通知 QML（远控端 Theme.qml 后续可据此动态换肤）
    Q_INVOKABLE void setTheme(bool dark, const QColor &accent);

    // 多窗口打开器：从 MinecraftServerManager 模块实例化对应 Window 类型（与桌面端一致）
    void setEngine(QQmlApplicationEngine *engine);
    Q_INVOKABLE void showMainWindow();
    Q_INVOKABLE void openDownloadCenter();
    Q_INVOKABLE void openCreateServer();
    Q_INVOKABLE void openImportModpack();
    Q_INVOKABLE void openControllerSettings();
    Q_INVOKABLE void closeAllWindows();
    Q_INVOKABLE void quitApp();

signals:
    void themeApplied(bool dark, const QColor &accent);
    void bgmEnabledChanged();
    void bgmVolumeChanged();
    void bgImagePathChanged();
    void bgScrimOpacityChanged();
    void uiTransparencyChanged();
    void bgLayerOpacityChanged();

private:
    QQuickWindow *createWindow(const QString &type);
    QQuickWindow *showUnique(const QString &type);

    RemoteClient *m_rc = nullptr;
    AppearanceSettings *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    QHash<QString, QQuickWindow *> m_unique;
    QList<QQuickWindow *> m_windows;
};
