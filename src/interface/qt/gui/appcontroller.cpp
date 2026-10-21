#include "appcontroller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QQmlContext>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>

// 根据深浅色 + 主色调构建并应用全局调色板：原生控件（托盘菜单/Fusion 样式）
// 随之变色；同时写入 QSettings 持久化，供下次启动加载（见 main）。
void AppController::applyTheme(bool dark, const QColor &accent)
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme/dark"), dark);
    settings.setValue(QStringLiteral("theme/accent"), accent);

    QPalette pal = QApplication::palette();
    if (dark) {
        pal.setColor(QPalette::Window, QColor("#1b1c21"));
        pal.setColor(QPalette::WindowText, QColor("#e8e8ec"));
        pal.setColor(QPalette::Base, QColor("#2c2f39"));
        pal.setColor(QPalette::AlternateBase, QColor("#24262e"));
        pal.setColor(QPalette::Text, QColor("#e8e8ec"));
        pal.setColor(QPalette::Button, QColor("#24262e"));
        pal.setColor(QPalette::ButtonText, QColor("#e8e8ec"));
        pal.setColor(QPalette::ToolTipBase, QColor("#24262e"));
        pal.setColor(QPalette::ToolTipText, QColor("#e8e8ec"));
    } else {
        pal.setColor(QPalette::Window, QColor("#f2f3f5"));
        pal.setColor(QPalette::WindowText, QColor("#1b1c21"));
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        pal.setColor(QPalette::AlternateBase, QColor("#e9eaee"));
        pal.setColor(QPalette::Text, QColor("#1b1c21"));
        pal.setColor(QPalette::Button, QColor("#ffffff"));
        pal.setColor(QPalette::ButtonText, QColor("#1b1c21"));
        pal.setColor(QPalette::ToolTipBase, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipText, QColor("#1b1c21"));
    }
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    QApplication::setPalette(pal);
}
QString AppController::appVersion() const { return QStringLiteral("v7.0"); }

bool AppController::bgmEnabled() const { return m_bgmEnabled; }

bool AppController::bgmAvailable() const { return m_bgmAvailable; }

double AppController::bgmVolume() const
{
    return m_bgm && m_bgm->audioOutput() ? m_bgm->audioOutput()->volume() : 0.5;
}

void AppController::setBgmEnabled(bool on)
{
    if (m_sc) {
        // 写入共享外观模型；播放器与 QML 通知由 bgmEnabledChanged 信号联动
        m_sc->setBgmEnabled(on);
    } else {
        m_bgmEnabled = on;
        QSettings s;
        s.setValue(QStringLiteral("ui/bgm"), on);
        if (on && m_bgmAvailable && m_bgm)
            m_bgm->play();
        else if (m_bgm)
            m_bgm->stop();
        emit bgmEnabledChanged();
    }
}

void AppController::setBgmVolume(double v)
{
    const double n = qBound(0.0, v, 1.0);
    if (m_sc) {
        // 写入共享外观模型；音量由 bgmVolumeChanged 信号联动到播放器
        m_sc->setBgmVolume(n);
    } else {
        if (m_bgm && m_bgm->audioOutput())
            m_bgm->audioOutput()->setVolume(n);
        emit bgmVolumeChanged();
    }
}

// 背景图在背景图目录下的绝对路径；未设置时返回空串（QML 据此隐藏背景层）
QString AppController::bgImageFullPath() const
{
    if (!m_sc || !m_sc->bgEnabled())
        return QString();
    const QStringList list = m_sc->bgImageList();
    if (!list.isEmpty()) {
        const int idx = qBound(0, m_sc->bgImageIndex(), list.size() - 1);
        // QML Image 需要 file:// URL，否则 Windows 本地路径 "d:/..." 会被当成协议 "d" 而加载失败
        return QUrl::fromLocalFile(list.at(idx)).toString();
    }
    // 兼容旧版单图设置：app/bgImage 存的是 skinDir 下相对文件名，拼出绝对路径回退，
    // 否则升级后老用户的旧壁纸会丢失（播放列表为空时原逻辑直接返回空）
    const QString oldPath = m_sc->bgImagePath();
    if (!oldPath.isEmpty()) {
        const QString full = SettingsController::bgImageDir() + QLatin1Char('/') + oldPath;
        if (QFile::exists(full))
            return QUrl::fromLocalFile(full).toString();
    }
    return QString();
}

bool AppController::bgImageVisible() const
{
    return !bgImageFullPath().isEmpty();
}

double AppController::bgScrimOpacity() const
{
    QSettings s;
    // 默认 0.45：既能看清图片，又保证控件可读
    return s.value(QStringLiteral("bg/scrimOpacity"), 0.45).toDouble();
}

void AppController::setBgScrimOpacity(double v)
{
    v = qBound(0.0, v, 1.0);
    QSettings s;
    s.setValue(QStringLiteral("bg/scrimOpacity"), v);
    emit bgScrimOpacityChanged();
}

double AppController::uiTransparency() const
{
    QSettings s;
    // 默认 1.0：完全不透明（各控件各自最小值在 QML 侧映射）
    return s.value(QStringLiteral("ui/transparency"), 1.0).toDouble();
}

void AppController::setUiTransparency(double v)
{
    v = qBound(0.0, v, 1.0);
    QSettings s;
    s.setValue(QStringLiteral("ui/transparency"), v);
    emit uiTransparencyChanged();
}

double AppController::bgLayerOpacity() const
{
    QSettings s;
    return s.value(QStringLiteral("ui/bgLayerOpacity"), 1.0).toDouble();
}

void AppController::setBgLayerOpacity(double v)
{
    v = qBound(0.0, v, 1.0);
    QSettings s;
    s.setValue(QStringLiteral("ui/bgLayerOpacity"), v);
    emit bgLayerOpacityChanged();
}

void AppController::setTheme(bool dark, const QColor &accent)
{
    applyTheme(dark, accent);
    // 持久化
    QSettings s;
    s.setValue(QStringLiteral("theme/dark"), dark);
    s.setValue(QStringLiteral("theme/accent"), accent);

    // 关键修复：Theme 是 pragma 单例，其 dark/accent 只在首次实例化时读取一次上下文属性，
    // 之后 setContextProperty 不会让它的绑定重新求值，导致切换深浅色/主题色界面无反应。
    // 因此直接拿到单例实例并写入属性，派生颜色（bg/panel/…）的绑定会随之重算。
    if (QObject *theme = m_engine->singletonInstance<QObject *>("MinecraftServerManager", "Theme")) {
        theme->setProperty("dark", dark);
        theme->setProperty("accent", QVariant::fromValue(accent));
    }
    emit themeApplied(dark, accent);
    // 同时同步上下文属性，避免其它直接引用 themeDark/themeAccent 的地方不一致
    m_engine->rootContext()->setContextProperty(QStringLiteral("themeDark"), dark);
    m_engine->rootContext()->setContextProperty(QStringLiteral("themeAccent"), accent);
}
