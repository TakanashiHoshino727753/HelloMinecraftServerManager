// appearancesettings.h —— 远控端外观个性化（背景图/背景音乐/壁纸轮换/透明度：用户选电脑文件，复制到 skin 目录）
// 与本地端 SettingsController 的壁纸能力对齐：单图 + 文件夹多图播放列表 + 顺序/随机轮换 + 压暗/透明度。
#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>

class AppearanceSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bgEnabled READ bgEnabled WRITE setBgEnabled NOTIFY bgEnabledChanged)
    Q_PROPERTY(QString bgImagePath READ bgImagePath NOTIFY bgImagePathChanged)
    Q_PROPERTY(QString bgImageFullPath READ bgImageFullPath NOTIFY bgImagePathChanged)
    Q_PROPERTY(bool bgImageVisible READ bgImageVisible NOTIFY bgImagePathChanged)
    Q_PROPERTY(bool bgmEnabled READ bgmEnabled WRITE setBgmEnabled NOTIFY bgmEnabledChanged)
    Q_PROPERTY(QString bgmPath READ bgmPath NOTIFY bgmPathChanged)
    Q_PROPERTY(double bgmVolume READ bgmVolume WRITE setBgmVolume NOTIFY bgmVolumeChanged)

    // 壁纸轮换（文件夹多图 + 顺序/随机）
    Q_PROPERTY(QString bgImageFolder READ bgImageFolder WRITE setBgImageFolder NOTIFY bgImageFolderChanged)
    Q_PROPERTY(QStringList bgImageList READ bgImageList NOTIFY bgImageListChanged)
    Q_PROPERTY(QString bgImageMode READ bgImageMode WRITE setBgImageMode NOTIFY bgImageModeChanged)
    Q_PROPERTY(int bgImageInterval READ bgImageInterval WRITE setBgImageInterval NOTIFY bgImageIntervalChanged)
    Q_PROPERTY(QString bgImageIntervalUnit READ bgImageIntervalUnit WRITE setBgImageIntervalUnit NOTIFY bgImageIntervalUnitChanged)
    Q_PROPERTY(int bgImageIndex READ bgImageIndex WRITE setBgImageIndex NOTIFY bgImageIndexChanged)

    // 压暗 / 透明度
    Q_PROPERTY(double bgScrimOpacity READ bgScrimOpacity WRITE setBgScrimOpacity NOTIFY bgScrimOpacityChanged)
    Q_PROPERTY(double uiTransparency READ uiTransparency WRITE setUiTransparency NOTIFY uiTransparencyChanged)
    Q_PROPERTY(double bgLayerOpacity READ bgLayerOpacity WRITE setBgLayerOpacity NOTIFY bgLayerOpacityChanged)

    // 标题栏自定义（各端各自管理：写本地 QSettings，不跨端同步）
    Q_PROPERTY(QString windowTitle READ windowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QString titleLayout READ titleLayout WRITE setTitleLayout NOTIFY titleLayoutChanged)
    Q_PROPERTY(QString titleImagePath READ titleImagePath NOTIFY titleImagePathChanged)
    Q_PROPERTY(QString titleImageUrl READ titleImageUrl NOTIFY titleImagePathChanged)

public:
    explicit AppearanceSettings(QObject *parent = nullptr);
    static QString skinDir();
    // 应用标识（桌面端="HMCSM"，远控端="HMCSM_Remote"）：把两端皮肤目录区分开，
    // 同一台机器同时装两端时互不干扰。未设置时默认 "HMCSM_Remote"（保持远控端历史行为）。
    static void setAppTag(const QString &t);
    static QString appTag();
    // 背景图目录：程序同目录/backgroundpic/
    static QString bgImageDir() {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/backgroundpic");
    }
    // 背景音乐目录：程序同目录/backgroundmusic/
    static QString bgmDir() {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/backgroundmusic");
    }

    bool bgEnabled() const { return m_bgEnabled; }
    QString bgImagePath() const { return m_bgImagePath; }
    bool bgImageVisible() const { return !bgImageFullPath().isEmpty(); }
    bool bgmEnabled() const { return m_bgmEnabled; }
    QString bgmPath() const { return m_bgmPath; }
    double bgmVolume() const { return m_bgmVolume; }

    QString bgImageFolder() const { return m_bgImageFolder; }
    QStringList bgImageList() const { return m_bgImageList; }
    QString bgImageMode() const { return m_bgImageMode; }
    int bgImageInterval() const { return m_bgImageInterval; }
    QString bgImageIntervalUnit() const { return m_bgImageIntervalUnit; }
    int bgImageIndex() const { return m_bgImageIndex; }

    double bgScrimOpacity() const { return m_bgScrimOpacity; }
    double uiTransparency() const { return m_uiTransparency; }
    double bgLayerOpacity() const { return m_bgLayerOpacity; }

    QString windowTitle() const { return m_windowTitle; }
    QString titleLayout() const { return m_titleLayout; }
    QString titleImagePath() const { return m_titleImagePath; }
    QString titleImageUrl() const {
        return m_titleImagePath.isEmpty() ? QString()
                                           : QUrl::fromLocalFile(skinDir() + QStringLiteral("/") + m_titleImagePath).toString();
    }

    Q_INVOKABLE void setBgEnabled(bool v);
    Q_INVOKABLE void setBgmEnabled(bool v);
    Q_INVOKABLE void setBgmVolume(double v);
    // 单图路径 setter（桌面端 SettingsController 委托过来用：webuiserver/PUT 会直接设置）
    Q_INVOKABLE void setBgImagePath(const QString &v);
    Q_INVOKABLE void setBgmPath(const QString &v);
    Q_INVOKABLE void setBgImageFolder(const QString &v);
    Q_INVOKABLE void setBgImageMode(const QString &v);
    Q_INVOKABLE void setBgImageInterval(int v);
    Q_INVOKABLE void setBgImageIntervalUnit(const QString &v);
    Q_INVOKABLE void setBgImageIndex(int v);
    Q_INVOKABLE void setBgScrimOpacity(double v);
    Q_INVOKABLE void setUiTransparency(double v);
    Q_INVOKABLE void setBgLayerOpacity(double v);

    Q_INVOKABLE void setWindowTitle(const QString &v);
    Q_INVOKABLE void setTitleLayout(const QString &v);
    Q_INVOKABLE void setTitleImagePath(const QString &v);

    Q_INVOKABLE bool importSkinImage(const QString &src);
    Q_INVOKABLE bool importSkinMusic(const QString &src);
    Q_INVOKABLE void clearSkinImage();
    Q_INVOKABLE void clearSkinMusic();
    Q_INVOKABLE bool importTitleImage(const QString &src);
    Q_INVOKABLE void clearTitleImage();
    Q_INVOKABLE QString bgImageFullPath() const;

    // 壁纸播放列表管理（与本地端 SettingsController 对齐）
    Q_INVOKABLE void addBgImages(const QStringList &files);
    Q_INVOKABLE void removeBgImage(int index);
    Q_INVOKABLE void moveBgImage(int from, int to);
    Q_INVOKABLE void clearBgImages();

signals:
    void bgEnabledChanged();
    void bgImagePathChanged();
    void bgmEnabledChanged();
    void bgmPathChanged();
    void bgmVolumeChanged();
    void bgImageFolderChanged();
    void bgImageListChanged();
    void bgImageModeChanged();
    void bgImageIntervalChanged();
    void bgImageIntervalUnitChanged();
    void bgImageIndexChanged();
    void bgScrimOpacityChanged();
    void uiTransparencyChanged();
    void bgLayerOpacityChanged();

    void windowTitleChanged();
    void titleLayoutChanged();
    void titleImagePathChanged();

private:
    static QString stripFileUrl(QString path) {
        if (path.startsWith(QStringLiteral("file:///")))
            path = path.mid(8);
        else if (path.startsWith(QStringLiteral("file://")))
            path = path.mid(7);
        return path;
    }
    // 壁纸轮换：定时器触发切下一张；依据模式（顺序/随机）更新当前索引并通知 QML 刷新
    void refreshRotation();
    void rotateBg();

    bool m_bgEnabled = false;
    QString m_bgImagePath;
    bool m_bgmEnabled = false;
    QString m_bgmPath;
    double m_bgmVolume = 0.5;

    // 壁纸轮换
    QString m_bgImageFolder;
    QStringList m_bgImageList;
    QString m_bgImageMode = QStringLiteral("sequential");
    int m_bgImageInterval = 10;          // 数值（配合单位解释）
    QString m_bgImageIntervalUnit = QStringLiteral("min");   // 轮换间隔单位：sec/min/hour/day
    int m_bgImageIndex = 0;

    double m_bgScrimOpacity = 0.45;      // 背景图压暗强度（0~1）
    double m_uiTransparency = 1.0;       // 界面控件透明度（0~1）
    double m_bgLayerOpacity = 1.0;       // 区域底色透明度（0~1）

    // 标题栏自定义
    QString m_windowTitle;
    QString m_titleLayout = QStringLiteral("text");
    QString m_titleImagePath;

    QTimer *m_rotTimer = nullptr;        // 壁纸轮换定时器

    static QString s_appTag;             // 应用标识（区分两端皮肤目录）
};
