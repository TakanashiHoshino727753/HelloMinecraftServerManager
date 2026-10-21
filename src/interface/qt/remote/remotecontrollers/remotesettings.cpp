#include "remotesettings.h"
#include "remoteclient.h"
#include <QNetworkReply>
#include <QJsonDocument>

// 远控端“个性化”外观字段：这些应作用于本地 AppearanceSettings（界面即时可见），而非代理被控端。
static const QStringList kAppearanceFields = {
    QStringLiteral("bgEnabled"), QStringLiteral("bgImagePath"), QStringLiteral("bgmEnabled"),
    QStringLiteral("bgmPath"), QStringLiteral("bgmVolume"), QStringLiteral("bgImageFolder"),
    QStringLiteral("bgImageList"), QStringLiteral("bgImageMode"), QStringLiteral("bgImageInterval"),
    QStringLiteral("bgImageIntervalUnit"), QStringLiteral("bgImageIndex"),
    QStringLiteral("windowTitle"), QStringLiteral("titleLayout"),
    QStringLiteral("titleImagePath"), QStringLiteral("titleImageUrl")
};
static bool isAppearanceField(const QString &k) { return kAppearanceFields.contains(k); }
static void applyAppearance(AppearanceSettings *a, const QString &field, const QVariant &value)
{
    if (!a) return;
    if (field == QStringLiteral("bgEnabled")) a->setBgEnabled(value.toBool());
    else if (field == QStringLiteral("bgImagePath")) a->importSkinImage(value.toString());
    else if (field == QStringLiteral("bgmEnabled")) a->setBgmEnabled(value.toBool());
    else if (field == QStringLiteral("bgmPath")) a->importSkinMusic(value.toString());
    else if (field == QStringLiteral("bgmVolume")) a->setBgmVolume(value.toDouble());
    else if (field == QStringLiteral("bgImageFolder")) a->setBgImageFolder(value.toString());
    else if (field == QStringLiteral("bgImageList")) { a->clearBgImages(); a->addBgImages(value.toStringList()); }
    else if (field == QStringLiteral("bgImageMode")) a->setBgImageMode(value.toString());
    else if (field == QStringLiteral("bgImageInterval")) a->setBgImageInterval(value.toInt());
    else if (field == QStringLiteral("bgImageIntervalUnit")) a->setBgImageIntervalUnit(value.toString());
    else if (field == QStringLiteral("bgImageIndex")) a->setBgImageIndex(value.toInt());
    else if (field == QStringLiteral("windowTitle")) a->setWindowTitle(value.toString());
    else if (field == QStringLiteral("titleLayout")) a->setTitleLayout(value.toString());
    else if (field == QStringLiteral("titleImagePath")) a->setTitleImagePath(value.toString());
    else if (field == QStringLiteral("titleImageUrl")) a->setTitleImagePath(value.toString());
}

RemoteSettingsController::RemoteSettingsController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteSettingsController::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}

void RemoteSettingsController::setAppearance(AppearanceSettings *a)
{
    m_appearance = a;
}

void RemoteSettingsController::merge(const QJsonObject &o)
{
    bool changed = false;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (isAppearanceField(it.key())) continue;   // 外观以本地为准，不从被控端覆盖
        if (m_data.value(it.key()) != it.value().toVariant()) {
            m_data.insert(it.key(), it.value().toVariant());
            changed = true;
        }
    }
    if (changed) emit dataChanged();
}

void RemoteSettingsController::refresh()
{
    auto onFin = [this](QNetworkReply *r) {
        if (r->error() == QNetworkReply::NoError) merge(QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    };
    QNetworkReply *a = m_rc->apiGet(QStringLiteral("/settings"));
    connect(a, &QNetworkReply::finished, this, [a, onFin]() { onFin(a); });
    QNetworkReply *b = m_rc->apiGet(QStringLiteral("/webui"));
    connect(b, &QNetworkReply::finished, this, [b, onFin]() { onFin(b); });
    QNetworkReply *c = m_rc->apiGet(QStringLiteral("/theme"));
    connect(c, &QNetworkReply::finished, this, [c, onFin]() { onFin(c); });
}

void RemoteSettingsController::set(const QString &field, const QVariant &value)
{
    // 远控端“个性化”外观为客户端本地设置：直接作用于本地 AppearanceSettings（远端界面即时可见），不代理被控端
    if (m_appearance && isAppearanceField(field)) {
        applyAppearance(m_appearance, field, value);
        m_data.insert(field, value);
        emit dataChanged();
        return;
    }
    if (field.startsWith(QStringLiteral("webui"))) {
        // 服务端 /webui PUT 按去掉 webui 前缀的键写入（enabled/port/token/exposeLan/certPath/keyPath）
        static const QHash<QString, QString> map = {
            {QStringLiteral("webuiEnabled"), QStringLiteral("enabled")},
            {QStringLiteral("webuiPort"), QStringLiteral("port")},
            {QStringLiteral("webuiToken"), QStringLiteral("token")},
            {QStringLiteral("webuiExposeLan"), QStringLiteral("exposeLan")},
            {QStringLiteral("webuiCertPath"), QStringLiteral("certPath")},
            {QStringLiteral("webuiKeyPath"), QStringLiteral("keyPath")}
        };
        const QString key = map.value(field, field);
        m_rc->apiPut(QStringLiteral("/webui"), QJsonObject{{key, QJsonValue::fromVariant(value)}});
    } else if (field == QStringLiteral("themeDark") || field == QStringLiteral("themeAccent")) {
        const QString key = (field == QStringLiteral("themeDark")) ? QStringLiteral("dark") : QStringLiteral("accent");
        m_rc->apiPut(QStringLiteral("/theme"), QJsonObject{{key, QJsonValue::fromVariant(value)}});
    } else {
        m_rc->apiPut(QStringLiteral("/settings"), QJsonObject{{field, QJsonValue::fromVariant(value)}});
    }
    m_data.insert(field, value);
    emit dataChanged();
}

void RemoteSettingsController::apply()
{
    // m_data 同时含 /webui(/theme 的键，需按键路由到对应端点，否则丢设置
    static const QStringList wsKeys = {QStringLiteral("enabled"), QStringLiteral("port"),
        QStringLiteral("token"), QStringLiteral("exposeLan"),
        QStringLiteral("certPath"), QStringLiteral("keyPath")};
    QJsonObject ws, theme, rest;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        const QString k = it.key();
        // 外观字段已在 set()/专用方法中即时应用到本地 AppearanceSettings（界面即时可见）；
        // 这里若再用（可能过期的）m_data 重复覆盖，会撤销刚做的背景/标题/模式改动，故跳过。
        if (isAppearanceField(k)) continue;
        if (wsKeys.contains(k)) ws.insert(k, QJsonValue::fromVariant(it.value()));
        else if (k == QStringLiteral("dark") || k == QStringLiteral("accent")) theme.insert(k, QJsonValue::fromVariant(it.value()));
        else rest.insert(k, QJsonValue::fromVariant(it.value()));
    }
    if (!rest.isEmpty()) m_rc->apiPut(QStringLiteral("/settings"), rest);
    if (!ws.isEmpty()) m_rc->apiPut(QStringLiteral("/webui"), ws);
    if (!theme.isEmpty()) m_rc->apiPut(QStringLiteral("/theme"), theme);
}

void RemoteSettingsController::regenerateWebuiToken()
{
    set(QStringLiteral("webuiToken"), QString());
}

void RemoteSettingsController::clearBgImages()
{
    if (m_appearance) m_appearance->clearBgImages();
    m_data.insert(QStringLiteral("bgImageList"), QVariantList());
    m_data.insert(QStringLiteral("bgImageFolder"), QString());
    m_data.insert(QStringLiteral("bgEnabled"), false);
    emit dataChanged();
}
void RemoteSettingsController::moveBgImage(int from, int to)
{
    if (m_appearance) m_appearance->moveBgImage(from, to);
    if (m_appearance) m_data.insert(QStringLiteral("bgImageList"), m_appearance->bgImageList());
    emit dataChanged();
}
void RemoteSettingsController::removeBgImage(int index)
{
    if (m_appearance) m_appearance->removeBgImage(index);
    if (m_appearance) m_data.insert(QStringLiteral("bgImageList"), m_appearance->bgImageList());
    emit dataChanged();
}
void RemoteSettingsController::addBgImages(const QStringList &files)
{
    if (m_appearance) m_appearance->addBgImages(files);
    if (m_appearance) m_data.insert(QStringLiteral("bgImageList"), m_appearance->bgImageList());
    emit dataChanged();
}
void RemoteSettingsController::clearSkinMusic()
{
    if (m_appearance) m_appearance->clearSkinMusic();
    m_data.insert(QStringLiteral("bgmPath"), QString());
    emit dataChanged();
}
void RemoteSettingsController::clearTitleImage()
{
    if (m_appearance) m_appearance->clearTitleImage();
    m_data.insert(QStringLiteral("titleImageUrl"), QString());
    m_data.insert(QStringLiteral("titleImagePath"), QString());
    emit dataChanged();
}
bool RemoteSettingsController::importTitleImage(const QString &path)
{
    bool ok = false;
    if (m_appearance) ok = m_appearance->importTitleImage(path);
    if (m_appearance) {
        m_data.insert(QStringLiteral("titleImagePath"), m_appearance->titleImagePath());
        m_data.insert(QStringLiteral("titleImageUrl"), m_appearance->titleImageUrl());
    }
    emit dataChanged();
    return ok;
}
void RemoteSettingsController::importSkinMusic(const QString &path)
{
    if (m_appearance) m_appearance->importSkinMusic(path);
    if (m_appearance) m_data.insert(QStringLiteral("bgmPath"), m_appearance->bgmPath());
    emit dataChanged();
}

RemoteBotController::RemoteBotController(RemoteClient *rc, QObject *parent)
    : QObject(parent), m_rc(rc)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &RemoteBotController::refresh);
    connect(m_rc, &RemoteClient::connectedChanged, this, [this]() {
        if (m_rc->connected()) { refresh(); m_timer->start(); } else m_timer->stop();
    });
}

void RemoteBotController::refresh()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/bot"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            bool changed = false;
            for (auto it = o.constBegin(); it != o.constEnd(); ++it)
                if (m_data.value(it.key()) != it.value().toVariant()) { m_data.insert(it.key(), it.value().toVariant()); changed = true; }
            if (changed) emit dataChanged();
        }
        r->deleteLater();
    });
}

void RemoteBotController::set(const QString &field, const QVariant &value)
{
    // 服务端 /bot 仅支持 GET/POST，且按直接键写入（enabled/linkedStart/napcatPath/...）
    m_rc->apiPost(QStringLiteral("/bot"), QJsonObject{{field, QJsonValue::fromVariant(value)}});
    m_data.insert(field, value);
    emit dataChanged();
}

void RemoteBotController::notify(const QString &message, const QString &scope)
{
    m_rc->apiPost(QStringLiteral("/bot/notify"), QJsonObject{{QStringLiteral("message"), message}, {QStringLiteral("scope"), scope}});
}

// —— 镜像 desktop BotController 探测方法：触发异步检测，返回最近一次缓存结果 ——
QVariantList RemoteBotController::detectNapcatPaths()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/bot/detect/napcat"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("napcatPaths"), o.value(QStringLiteral("paths")).toArray().toVariantList());
            m_data.insert(QStringLiteral("napcatPath"), o.value(QStringLiteral("path")).toVariant());
            emit dataChanged();
        }
        r->deleteLater();
    });
    return m_data.value(QStringLiteral("napcatPaths")).toList();
}
QString RemoteBotController::detectNapcatPath()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/bot/detect/napcat"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("napcatPath"), o.value(QStringLiteral("path")).toVariant());
            emit dataChanged();
        }
        r->deleteLater();
    });
    return m_data.value(QStringLiteral("napcatPath")).toString();
}
QVariantList RemoteBotController::detectNonebotDirs()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/bot/detect/nonebot"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("nonebotDirs"), o.value(QStringLiteral("dirs")).toArray().toVariantList());
            m_data.insert(QStringLiteral("nonebotDir"), o.value(QStringLiteral("dir")).toVariant());
            emit dataChanged();
        }
        r->deleteLater();
    });
    return m_data.value(QStringLiteral("nonebotDirs")).toList();
}
QString RemoteBotController::detectNonebotDir()
{
    QNetworkReply *r = m_rc->apiGet(QStringLiteral("/bot/detect/nonebot"));
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_data.insert(QStringLiteral("nonebotDir"), o.value(QStringLiteral("dir")).toVariant());
            emit dataChanged();
        }
        r->deleteLater();
    });
    return m_data.value(QStringLiteral("nonebotDir")).toString();
}
bool RemoteBotController::installHmcsmPlugin()
{
    m_rc->apiPost(QStringLiteral("/bot/plugin/install"));
    return true;
}
void RemoteBotController::retryStartNonebot()
{
    m_rc->apiPost(QStringLiteral("/bot/retry"));
}
