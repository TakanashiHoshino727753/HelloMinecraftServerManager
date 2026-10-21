/*
 * settingscontroller.cpp —— HMCSM 自身设置持久化
 * -------------------------------------------------
 * 开机自启（Windows 注册表 Run 键）、语言、WebUI 开关/端口、
 * QQ 机器人（NapCat / NoneBot）开关与路径、默认服务器目录。
 * 个性化（背景图/背景音乐/标题栏/壁纸轮换）统一委托给共享的 AppearanceSettings，
 * 与远控端共用同一实现（单一数据源）。
 * 所有值经 QSettings(HMCSM) 读写；主题状态在 AppController 持久化。
 */
#include "settingscontroller.h"
#include "appearancesettings.h"
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const char *RUN_KEY = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 生成 32 位十六进制访问令牌
static QString generateWebuiToken()
{
    QString out;
    out.reserve(32);
    QRandomGenerator *rng = QRandomGenerator::system();
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
        out.append(QLatin1Char(hex[rng->bounded(16)]));
    return out;
}

SettingsController::SettingsController(QObject *parent) : QObject(parent)
{
    // 统一经由默认 QSettings()（作用域由 main 设为 HMCSM/MinecraftServerManager）。
    QSettings s;
    m_language = s.value(QStringLiteral("app/language"), QStringLiteral("简体中文")).toString();
    m_webui = s.value(QStringLiteral("app/webui"), true).toBool();
    m_webuiPort = s.value(QStringLiteral("app/webuiPort"), 25575).toInt();
    m_webuiToken = s.value(QStringLiteral("app/webuiToken")).toString();
    if (m_webuiToken.isEmpty())
        m_webuiToken = generateWebuiToken();   // 首次运行自动生成，避免无令牌导致接口裸奔
    // Linux 通常是服务器/VM 部署，默认暴露到局域网(0.0.0.0)以便远程访问；
    // Windows 桌面端默认仅本机(127.0.0.1)最安全。令牌校验在两类平台上都强制生效。
#ifdef Q_OS_LINUX
    m_webuiExposeLan = s.value(QStringLiteral("app/webuiExposeLan"), true).toBool();
#else
    // Windows 桌面端默认也暴露到局域网，方便手机 App 扫码直连本机控制台。
    // 令牌校验在两种平台都强制生效，安全性不受影响。
    m_webuiExposeLan = s.value(QStringLiteral("app/webuiExposeLan"), true).toBool();
#endif
    m_webuiCertPath = s.value(QStringLiteral("app/webuiCertPath")).toString();
    m_webuiKeyPath = s.value(QStringLiteral("app/webuiKeyPath")).toString();
    m_showOnStartup = s.value(QStringLiteral("app/showOnStartup"), false).toBool();
    m_napcat = s.value(QStringLiteral("app/napcat"), false).toBool();
    m_napcatPath = s.value(QStringLiteral("app/napcatPath")).toString();
    m_nonebot = s.value(QStringLiteral("app/nonebot"), false).toBool();
    m_nonebotDir = s.value(QStringLiteral("app/nonebotDir")).toString();
    m_botUsageInterval = s.value(QStringLiteral("app/botUsageInterval"), 300).toInt();
    m_botLinkedStart = s.value(QStringLiteral("app/botLinkedStart"), false).toBool();
    m_bot = s.value(QStringLiteral("app/botEnabled"), m_napcat && m_nonebot).toBool();

    m_webhookUrl = s.value(QStringLiteral("app/webhookUrl")).toString();
    m_webhookType = s.value(QStringLiteral("app/webhookType"), QStringLiteral("discord")).toString();
    m_webhookEnabled = s.value(QStringLiteral("app/webhookEnabled"), false).toBool();
    m_webhookCrash = s.value(QStringLiteral("app/webhookCrash"), true).toBool();
    m_webhookState = s.value(QStringLiteral("app/webhookState"), true).toBool();
    m_webhookPlayer = s.value(QStringLiteral("app/webhookPlayer"), false).toBool();

    // 个性化（背景图/背景音乐/标题栏/壁纸轮换）已统一由共享的 AppearanceSettings 持有，
    // 此处不再加载；main() 通过 setAppearance() 注入共享模型。

    loadAutoStart();
}

void SettingsController::loadAutoStart()
{
    QString v;
#ifdef Q_OS_WIN
    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER,
                             "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, KEY_QUERY_VALUE, &hKey);
    if (res == ERROR_SUCCESS) {
        char buf[1024];
        DWORD sz = sizeof(buf);
        if (RegQueryValueExA(hKey, "HMCSM", nullptr, nullptr,
                             reinterpret_cast<BYTE *>(buf), &sz) == ERROR_SUCCESS)
            v = QString::fromLocal8Bit(buf);
        RegCloseKey(hKey);
    }
#else
    QSettings reg(QString::fromLocal8Bit(RUN_KEY), QSettings::NativeFormat);
    v = reg.value(QStringLiteral("HMCSM")).toString();
#endif
    // 去掉可能存在的引号（保存时按空格加的），并规范化路径（/ 与 \、大小写）再比较
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
        v = v.mid(1, v.size() - 2);
    const QString cur = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    m_autoStart = !v.isEmpty() && QFileInfo(v).canonicalFilePath() == cur;
}

void SettingsController::saveAutoStart()
{
    // 路径含空格时必须加引号，否则 Run 键启动时按空格拆分命令行，自启动失败。
    // 用 toNativeSeparators 把路径统一成 Windows 原生反斜杠，避免注册表里出现 "/"。
    QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (path.contains(QLatin1Char(' ')))
        path = QStringLiteral("\"%1\"").arg(path);

#ifdef Q_OS_WIN
    // 直接用 Windows API 写 Run 键，避免 QSettings 在 GUI 子系统下偶发的注册表路径歧义。
    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExA(HKEY_CURRENT_USER,
                             "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, KEY_SET_VALUE, &hKey);
    if (res == ERROR_SUCCESS) {
        if (m_autoStart) {
            const QByteArray data = path.toLocal8Bit();
            RegSetValueExA(hKey, "HMCSM", 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(data.constData()),
                           static_cast<DWORD>(data.size() + 1));
        } else {
            RegDeleteValueA(hKey, "HMCSM");
        }
        RegCloseKey(hKey);
    }
#else
    QSettings reg(QString::fromLocal8Bit(RUN_KEY), QSettings::NativeFormat);
    if (m_autoStart)
        reg.setValue(QStringLiteral("HMCSM"), path);
    else
        reg.remove(QStringLiteral("HMCSM"));
    reg.sync();
#endif
}

void SettingsController::setAutoStart(bool v)
{
    if (m_autoStart != v) { m_autoStart = v; emit autoStartChanged(); }
}
void SettingsController::setLanguage(const QString &v)
{
    if (m_language != v) { m_language = v; emit languageChanged(); }
}
void SettingsController::setWebuiEnabled(bool v)
{
    if (m_webui != v) { m_webui = v; emit webuiEnabledChanged(); }
}
void SettingsController::setWebuiPort(int v)
{
    if (m_webuiPort != v) { m_webuiPort = v; emit webuiPortChanged(); }
}
void SettingsController::setWebuiToken(const QString &v)
{
    const QString t = v.trimmed();
    if (m_webuiToken != t) { m_webuiToken = t; emit webuiTokenChanged(); }
}
void SettingsController::setWebuiExposeLan(bool v)
{
    if (m_webuiExposeLan != v) { m_webuiExposeLan = v; emit webuiExposeLanChanged(); }
}
void SettingsController::setWebuiCertPath(const QString &v)
{
    const QString p = stripFileUrl(v);
    if (m_webuiCertPath != p) { m_webuiCertPath = p; emit webuiCertPathChanged(); }
}
void SettingsController::setWebuiKeyPath(const QString &v)
{
    const QString p = stripFileUrl(v);
    if (m_webuiKeyPath != p) { m_webuiKeyPath = p; emit webuiKeyPathChanged(); }
}

void SettingsController::setShowOnStartup(bool v)
{
    if (m_showOnStartup != v) { m_showOnStartup = v; emit showOnStartupChanged(); }
}
void SettingsController::setWebhookUrl(const QString &v)
{
    if (m_webhookUrl != v) { m_webhookUrl = v; emit webhookUrlChanged(); }
}
void SettingsController::setWebhookType(const QString &v)
{
    if (m_webhookType != v) { m_webhookType = v; emit webhookTypeChanged(); }
}
void SettingsController::setWebhookEnabled(bool v)
{
    if (m_webhookEnabled != v) { m_webhookEnabled = v; emit webhookEnabledChanged(); }
}
void SettingsController::setWebhookCrash(bool v)
{
    if (m_webhookCrash != v) { m_webhookCrash = v; emit webhookCrashChanged(); }
}
void SettingsController::setWebhookState(bool v)
{
    if (m_webhookState != v) { m_webhookState = v; emit webhookStateChanged(); }
}
void SettingsController::setWebhookPlayer(bool v)
{
    if (m_webhookPlayer != v) { m_webhookPlayer = v; emit webhookPlayerChanged(); }
}
void SettingsController::regenerateWebuiToken()
{
    m_webuiToken = generateWebuiToken();
    emit webuiTokenChanged();
}
void SettingsController::setNapcatEnabled(bool v)
{
    if (m_napcat != v) { m_napcat = v; emit napcatEnabledChanged(); }
}
void SettingsController::setNapcatPath(const QString &v)
{
    const QString p = stripFileUrl(v);
    if (m_napcatPath != p) { m_napcatPath = p; emit napcatPathChanged(); }
}
void SettingsController::setNonebotEnabled(bool v)
{
    if (m_nonebot != v) { m_nonebot = v; emit nonebotEnabledChanged(); }
}
void SettingsController::setNonebotDir(const QString &v)
{
    const QString p = stripFileUrl(v);
    if (m_nonebotDir != p) { m_nonebotDir = p; emit nonebotDirChanged(); }
}
void SettingsController::setBotUsageInterval(int v)
{
    const int n = qMax(0, v);
    if (m_botUsageInterval != n) { m_botUsageInterval = n; emit botUsageIntervalChanged(); }
}

void SettingsController::setBotLinkedStart(bool v)
{
    if (m_botLinkedStart != v) { m_botLinkedStart = v; emit botLinkedStartChanged(); }
}

void SettingsController::setBotEnabled(bool v)
{
    if (m_bot == v)
        return;
    m_bot = v;
    // 主开关同时驱动两个子开关的内部状态，保持 /api/status 等逻辑一致
    m_napcat = v;
    m_nonebot = v;
    emit botEnabledChanged();
    emit napcatEnabledChanged();
    emit nonebotEnabledChanged();
}

void SettingsController::apply()
{
    // 统一经由默认 QSettings()；表驱动写入，消除样板。
    // 注：个性化字段不再在此持久化（已由 AppearanceSettings 自行落盘）。
    QSettings s;
    static const char *const keys[] = {
        "app/language", "app/webui", "app/webuiPort", "app/webuiToken",
        "app/webuiExposeLan", "app/webuiCertPath", "app/webuiKeyPath",
        "app/showOnStartup", "app/napcat", "app/napcatPath", "app/nonebot",
        "app/nonebotDir", "app/botUsageInterval", "app/botLinkedStart",
        "app/botEnabled", "app/webhookUrl", "app/webhookType",
        "app/webhookEnabled", "app/webhookCrash", "app/webhookState",
        "app/webhookPlayer",
    };
    const QVariant vals[] = {
        m_language, m_webui, m_webuiPort, m_webuiToken, m_webuiExposeLan,
        m_webuiCertPath, m_webuiKeyPath, m_showOnStartup, m_napcat, m_napcatPath,
        m_nonebot, m_nonebotDir, m_botUsageInterval, m_botLinkedStart, m_bot,
        m_webhookUrl, m_webhookType, m_webhookEnabled, m_webhookCrash,
        m_webhookState, m_webhookPlayer,
    };
    static_assert(sizeof(keys) / sizeof(*keys) == sizeof(vals) / sizeof(*vals),
                  "settings key/value count mismatch");
    const int n = static_cast<int>(sizeof(keys) / sizeof(*keys));
    for (int i = 0; i < n; ++i)
        s.setValue(QString::fromLatin1(keys[i]), vals[i]);

    s.sync();
    saveAutoStart();
}

// ============================================================================
// 个性化：全部委托给共享的 AppearanceSettings（与远控端同一实现，单一数据源）。
// 这样桌面端与远控端共用同一套个性化代码，不再各自维护一份、避免行为分叉。
// ============================================================================

void SettingsController::setAppearance(AppearanceSettings *a)
{
    m_app = a;
    if (!m_app)
        return;
    // 把共享模型的信号转发为本控制器同名信号，使既有的 QML 绑定/接口无需改动。
    connect(m_app, &AppearanceSettings::bgEnabledChanged, this, &SettingsController::bgEnabledChanged);
    connect(m_app, &AppearanceSettings::bgImagePathChanged, this, &SettingsController::bgImagePathChanged);
    connect(m_app, &AppearanceSettings::bgmEnabledChanged, this, &SettingsController::bgmEnabledChanged);
    connect(m_app, &AppearanceSettings::bgmPathChanged, this, &SettingsController::bgmPathChanged);
    connect(m_app, &AppearanceSettings::bgmVolumeChanged, this, &SettingsController::bgmVolumeChanged);
    connect(m_app, &AppearanceSettings::windowTitleChanged, this, &SettingsController::windowTitleChanged);
    connect(m_app, &AppearanceSettings::titleImagePathChanged, this, &SettingsController::titleImagePathChanged);
    connect(m_app, &AppearanceSettings::titleLayoutChanged, this, &SettingsController::titleLayoutChanged);
    connect(m_app, &AppearanceSettings::bgImageFolderChanged, this, &SettingsController::bgImageFolderChanged);
    connect(m_app, &AppearanceSettings::bgImageListChanged, this, &SettingsController::bgImageListChanged);
    connect(m_app, &AppearanceSettings::bgImageModeChanged, this, &SettingsController::bgImageModeChanged);
    connect(m_app, &AppearanceSettings::bgImageIntervalChanged, this, &SettingsController::bgImageIntervalChanged);
    connect(m_app, &AppearanceSettings::bgImageIntervalUnitChanged, this, &SettingsController::bgImageIntervalUnitChanged);
    connect(m_app, &AppearanceSettings::bgImageIndexChanged, this, &SettingsController::bgImageIndexChanged);
}

void SettingsController::setBgEnabled(bool v) { if (m_app) m_app->setBgEnabled(v); }
void SettingsController::setBgImagePath(const QString &v) { if (m_app) m_app->setBgImagePath(v); }
void SettingsController::setBgmEnabled(bool v) { if (m_app) m_app->setBgmEnabled(v); }
void SettingsController::setBgmPath(const QString &v) { if (m_app) m_app->setBgmPath(v); }
void SettingsController::setBgmVolume(double v) { if (m_app) m_app->setBgmVolume(v); }

void SettingsController::setWindowTitle(const QString &v) { if (m_app) m_app->setWindowTitle(v); }
void SettingsController::setTitleImagePath(const QString &v) { if (m_app) m_app->setTitleImagePath(v); }
void SettingsController::setTitleLayout(const QString &v) { if (m_app) m_app->setTitleLayout(v); }
QString SettingsController::titleImageUrl() const { return m_app ? m_app->titleImageUrl() : QString(); }

bool SettingsController::importSkinImage(const QString &srcFile) { return m_app ? m_app->importSkinImage(srcFile) : false; }
bool SettingsController::importSkinMusic(const QString &srcFile) { return m_app ? m_app->importSkinMusic(srcFile) : false; }
void SettingsController::clearSkinImage() { if (m_app) m_app->clearSkinImage(); }
void SettingsController::clearSkinMusic() { if (m_app) m_app->clearSkinMusic(); }
bool SettingsController::importTitleImage(const QString &srcFile) { return m_app ? m_app->importTitleImage(srcFile) : false; }
void SettingsController::clearTitleImage() { if (m_app) m_app->clearTitleImage(); }

void SettingsController::setBgImageFolder(const QString &v) { if (m_app) m_app->setBgImageFolder(v); }
void SettingsController::addBgImages(const QStringList &files) { if (m_app) m_app->addBgImages(files); }
void SettingsController::removeBgImage(int index) { if (m_app) m_app->removeBgImage(index); }
void SettingsController::moveBgImage(int from, int to) { if (m_app) m_app->moveBgImage(from, to); }
void SettingsController::clearBgImages() { if (m_app) m_app->clearBgImages(); }
void SettingsController::setBgImageMode(const QString &v) { if (m_app) m_app->setBgImageMode(v); }
void SettingsController::setBgImageInterval(int v) { if (m_app) m_app->setBgImageInterval(v); }
void SettingsController::setBgImageIntervalUnit(const QString &v) { if (m_app) m_app->setBgImageIntervalUnit(v); }
void SettingsController::setBgImageIndex(int v) { if (m_app) m_app->setBgImageIndex(v); }
