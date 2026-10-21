#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include "appearancesettings.h"
class RemoteClient;

// 远端设置（镜像 desktop SettingsController + 主题/webui 设置）：统一以 data 映射存储，
// 同时暴露与桌面端同名的具名属性/方法，使照搬来的桌面 QML 无需改动即可绑定。
class RemoteSettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    // —— 镜像 desktop SettingsController 具名属性（数据统一存于 m_data）——
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY dataChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle WRITE setWindowTitle NOTIFY dataChanged)
    Q_PROPERTY(QString titleLayout READ titleLayout WRITE setTitleLayout NOTIFY dataChanged)
    Q_PROPERTY(QString titleImageUrl READ titleImageUrl WRITE setTitleImageUrl NOTIFY dataChanged)
    Q_PROPERTY(QString titleImagePath READ titleImagePath WRITE setTitleImagePath NOTIFY dataChanged)
    Q_PROPERTY(bool bgEnabled READ bgEnabled WRITE setBgEnabled NOTIFY dataChanged)
    Q_PROPERTY(QVariantList bgImageList READ bgImageList WRITE setBgImageList NOTIFY dataChanged)
    Q_PROPERTY(QString bgImageFolder READ bgImageFolder WRITE setBgImageFolder NOTIFY dataChanged)
    Q_PROPERTY(int bgImageIndex READ bgImageIndex WRITE setBgImageIndex NOTIFY dataChanged)
    Q_PROPERTY(QString bgImageMode READ bgImageMode WRITE setBgImageMode NOTIFY dataChanged)
    Q_PROPERTY(int bgImageInterval READ bgImageInterval WRITE setBgImageInterval NOTIFY dataChanged)
    Q_PROPERTY(QString bgImageIntervalUnit READ bgImageIntervalUnit WRITE setBgImageIntervalUnit NOTIFY dataChanged)
    Q_PROPERTY(bool bgmEnabled READ bgmEnabled WRITE setBgmEnabled NOTIFY dataChanged)
    Q_PROPERTY(QString bgmPath READ bgmPath WRITE setBgmPath NOTIFY dataChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY dataChanged)
    Q_PROPERTY(bool showOnStartup READ showOnStartup WRITE setShowOnStartup NOTIFY dataChanged)
    Q_PROPERTY(QString defaultServerDir READ defaultServerDir WRITE setDefaultServerDir NOTIFY dataChanged)
    Q_PROPERTY(bool webuiEnabled READ webuiEnabled WRITE setWebuiEnabled NOTIFY dataChanged)
    Q_PROPERTY(int webuiPort READ webuiPort WRITE setWebuiPort NOTIFY dataChanged)
    Q_PROPERTY(QString webuiToken READ webuiToken WRITE setWebuiToken NOTIFY dataChanged)
    Q_PROPERTY(bool webuiExposeLan READ webuiExposeLan WRITE setWebuiExposeLan NOTIFY dataChanged)
    Q_PROPERTY(QString webuiCertPath READ webuiCertPath NOTIFY dataChanged)
    Q_PROPERTY(QString webuiKeyPath READ webuiKeyPath NOTIFY dataChanged)
    Q_PROPERTY(bool botLinkedStart READ botLinkedStart WRITE setBotLinkedStart NOTIFY dataChanged)
    Q_PROPERTY(bool botEnabled READ botEnabled WRITE setBotEnabled NOTIFY dataChanged)
    Q_PROPERTY(QString napcatPath READ napcatPath WRITE setNapcatPath NOTIFY dataChanged)
    Q_PROPERTY(QString nonebotDir READ nonebotDir WRITE setNonebotDir NOTIFY dataChanged)
    Q_PROPERTY(int botUsageInterval READ botUsageInterval WRITE setBotUsageInterval NOTIFY dataChanged)
    Q_PROPERTY(bool webhookEnabled READ webhookEnabled WRITE setWebhookEnabled NOTIFY dataChanged)
    Q_PROPERTY(QString webhookType READ webhookType WRITE setWebhookType NOTIFY dataChanged)
    Q_PROPERTY(QString webhookUrl READ webhookUrl WRITE setWebhookUrl NOTIFY dataChanged)
    Q_PROPERTY(bool webhookCrash READ webhookCrash WRITE setWebhookCrash NOTIFY dataChanged)
    Q_PROPERTY(bool webhookState READ webhookState WRITE setWebhookState NOTIFY dataChanged)
    Q_PROPERTY(bool webhookPlayer READ webhookPlayer WRITE setWebhookPlayer NOTIFY dataChanged)
public:
    explicit RemoteSettingsController(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }

    QString language() const { return m_data.value("language").toString(); }
    // 标题栏也属“个性化”，各端各自管理：读取本地 AppearanceSettings，不从被控端覆盖
    QString windowTitle() const { return m_appearance ? m_appearance->windowTitle() : m_data.value("windowTitle").toString(); }
    QString titleLayout() const { return m_appearance ? m_appearance->titleLayout() : m_data.value("titleLayout").toString(); }
    QString titleImageUrl() const { return m_appearance ? m_appearance->titleImageUrl() : m_data.value("titleImageUrl").toString(); }
    QString titleImagePath() const { return m_appearance ? m_appearance->titleImagePath() : m_data.value("titleImagePath").toString(); }
    // 远控端“个性化”外观为客户端本地设置：读取本地 AppearanceSettings（界面即时可见），不从被控端覆盖。
    bool bgEnabled() const { return m_appearance ? m_appearance->bgEnabled() : m_data.value("bgEnabled").toBool(); }
    QVariantList bgImageList() const {
        if (m_appearance) { QVariantList v; for (const QString &s : m_appearance->bgImageList()) v.append(s); return v; }
        return m_data.value("bgImageList").toList();
    }
    QString bgImageFolder() const { return m_appearance ? m_appearance->bgImageFolder() : m_data.value("bgImageFolder").toString(); }
    int bgImageIndex() const { return m_appearance ? m_appearance->bgImageIndex() : m_data.value("bgImageIndex").toInt(); }
    QString bgImageMode() const { return m_appearance ? m_appearance->bgImageMode() : m_data.value("bgImageMode").toString(); }
    int bgImageInterval() const { return m_appearance ? m_appearance->bgImageInterval() : m_data.value("bgImageInterval").toInt(); }
    QString bgImageIntervalUnit() const { return m_appearance ? m_appearance->bgImageIntervalUnit() : m_data.value("bgImageIntervalUnit").toString(); }
    bool bgmEnabled() const { return m_appearance ? m_appearance->bgmEnabled() : m_data.value("bgmEnabled").toBool(); }
    QString bgmPath() const { return m_appearance ? m_appearance->bgmPath() : m_data.value("bgmPath").toString(); }
    bool autoStart() const { return m_data.value("autoStart").toBool(); }
    bool showOnStartup() const { return m_data.value("showOnStartup").toBool(); }
    QString defaultServerDir() const { return m_data.value("defaultServerDir").toString(); }
    bool webuiEnabled() const { return m_data.value("webuiEnabled").toBool(); }
    int webuiPort() const { return m_data.value("webuiPort").toInt(); }
    QString webuiToken() const { return m_data.value("webuiToken").toString(); }
    bool webuiExposeLan() const { return m_data.value("webuiExposeLan").toBool(); }
    QString webuiCertPath() const { return m_data.value("webuiCertPath").toString(); }
    QString webuiKeyPath() const { return m_data.value("webuiKeyPath").toString(); }
    bool botLinkedStart() const { return m_data.value("botLinkedStart").toBool(); }
    bool botEnabled() const { return m_data.value("botEnabled").toBool(); }
    QString napcatPath() const { return m_data.value("napcatPath").toString(); }
    QString nonebotDir() const { return m_data.value("nonebotDir").toString(); }
    int botUsageInterval() const { return m_data.value("botUsageInterval").toInt(); }
    bool webhookEnabled() const { return m_data.value("webhookEnabled").toBool(); }
    QString webhookType() const { return m_data.value("webhookType").toString(); }
    QString webhookUrl() const { return m_data.value("webhookUrl").toString(); }
    bool webhookCrash() const { return m_data.value("webhookCrash").toBool(); }
    bool webhookState() const { return m_data.value("webhookState").toBool(); }
    bool webhookPlayer() const { return m_data.value("webhookPlayer").toBool(); }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &field, const QVariant &value);
    void setAppearance(AppearanceSettings *a);   // 绑定本地外观对象，使“个性化”生效于远端自身界面
    Q_INVOKABLE void apply();
    Q_INVOKABLE void regenerateWebuiToken();

    // 壁纸 / 标题 / 皮肤方法（镜像 desktop SettingsController）
    Q_INVOKABLE void clearBgImages();
    Q_INVOKABLE void moveBgImage(int from, int to);
    Q_INVOKABLE void removeBgImage(int index);
    Q_INVOKABLE void addBgImages(const QStringList &files);
    Q_INVOKABLE void clearSkinMusic();
    Q_INVOKABLE void clearTitleImage();
    Q_INVOKABLE bool importTitleImage(const QString &path);
    Q_INVOKABLE void importSkinMusic(const QString &path);

    // 具名属性 WRITE（桥接到 set()/apply()）
    void setLanguage(const QString &v) { set("language", v); }
    void setWindowTitle(const QString &v) { set("windowTitle", v); }
    void setTitleLayout(const QString &v) { set("titleLayout", v); }
    void setTitleImageUrl(const QString &v) { set("titleImageUrl", v); }
    void setTitleImagePath(const QString &v) { set("titleImagePath", v); }
    void setBgEnabled(bool v) { set("bgEnabled", v); }
    void setBgImageList(const QVariantList &v) { set("bgImageList", v); }
    void setBgImageFolder(const QString &v) { set("bgImageFolder", v); }
    void setBgImageIndex(int v) { set("bgImageIndex", v); }
    void setBgImageMode(const QString &v) { set("bgImageMode", v); }
    void setBgImageInterval(int v) { set("bgImageInterval", v); }
    void setBgImageIntervalUnit(const QString &v) { set("bgImageIntervalUnit", v); }
    void setBgmEnabled(bool v) { set("bgmEnabled", v); }
    void setBgmPath(const QString &v) { set("bgmPath", v); }
    void setAutoStart(bool v) { set("autoStart", v); }
    void setShowOnStartup(bool v) { set("showOnStartup", v); }
    void setDefaultServerDir(const QString &v) { set("defaultServerDir", v); }
    void setWebuiEnabled(bool v) { set("webuiEnabled", v); }
    void setWebuiPort(int v) { set("webuiPort", v); }
    void setWebuiToken(const QString &v) { set("webuiToken", v); }
    void setWebuiExposeLan(bool v) { set("webuiExposeLan", v); }
    void setBotLinkedStart(bool v) { set("botLinkedStart", v); }
    void setBotEnabled(bool v) { set("botEnabled", v); }
    void setNapcatPath(const QString &v) { set("napcatPath", v); }
    void setNonebotDir(const QString &v) { set("nonebotDir", v); }
    void setBotUsageInterval(int v) { set("botUsageInterval", v); }
    void setWebhookEnabled(bool v) { set("webhookEnabled", v); }
    void setWebhookType(const QString &v) { set("webhookType", v); }
    void setWebhookUrl(const QString &v) { set("webhookUrl", v); }
    void setWebhookCrash(bool v) { set("webhookCrash", v); }
    void setWebhookState(bool v) { set("webhookState", v); }
    void setWebhookPlayer(bool v) { set("webhookPlayer", v); }
signals:
    void dataChanged();
private:
    void merge(const QJsonObject &o);
    RemoteClient *m_rc;
    AppearanceSettings *m_appearance = nullptr;
    QVariantMap m_data;
    QTimer *m_timer;
};

// 远端 Bot 控制（镜像 desktop BotController）：data 映射 + 与桌面同名的具名属性/探测方法
class RemoteBotController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    Q_PROPERTY(QString hmcsmPluginState READ hmcsmPluginState NOTIFY dataChanged)
    Q_PROPERTY(QString napcatState READ napcatState NOTIFY dataChanged)
    Q_PROPERTY(QString nonebotState READ nonebotState NOTIFY dataChanged)
    Q_PROPERTY(int controlPort READ controlPort NOTIFY dataChanged)
    Q_PROPERTY(bool botEnabled READ botEnabled NOTIFY dataChanged)
public:
    explicit RemoteBotController(RemoteClient *rc, QObject *parent = nullptr);
    QVariantMap data() const { return m_data; }
    QString hmcsmPluginState() const { return m_data.value("hmcsmPluginState").toString(); }
    QString napcatState() const { return m_data.value("napcatState").toString(); }
    QString nonebotState() const { return m_data.value("nonebotState").toString(); }
    int controlPort() const { return m_data.value("controlPort").toInt(); }
    bool botEnabled() const { return m_data.value("botEnabled").toBool(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &field, const QVariant &value);
    Q_INVOKABLE void notify(const QString &message, const QString &scope = QString());
    // 镜像 desktop BotController 的同名方法（结果缓存于 m_data，调用时触发异步刷新）
    Q_INVOKABLE QVariantList detectNapcatPaths();
    Q_INVOKABLE QString detectNapcatPath();
    Q_INVOKABLE QVariantList detectNonebotDirs();
    Q_INVOKABLE QString detectNonebotDir();
    Q_INVOKABLE bool installHmcsmPlugin();
    Q_INVOKABLE void retryStartNonebot();
signals:
    void dataChanged();
    void pluginMissing(const QString &what);
private:
    RemoteClient *m_rc;
    QVariantMap m_data;
    QTimer *m_timer;
};
