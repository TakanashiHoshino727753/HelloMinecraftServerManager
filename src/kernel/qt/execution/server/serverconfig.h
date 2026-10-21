#pragma once
#include <QObject>
#include <QVariantMap>
#include <QSet>

// ============================================================================
// Server Configuration File Management（服务端配置文件管理）
// ----------------------------------------------------------------------------
// 负责 server.properties 的读写，以及 server-port 的探测 / 端口空闲检测 / 自动分配。
// 纯文件操作，不依赖任何运行态；assignFreePort 需要的「已占用端口集合」由调用方传入。
// ============================================================================

class ServerConfig : public QObject
{
    Q_OBJECT
public:
    explicit ServerConfig(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap readProperties(const QString &serverPath);
    Q_INVOKABLE void writeProperties(const QString &serverPath, const QVariantMap &map);

    Q_INVOKABLE int serverPort(const QString &serverPath) const;
    Q_INVOKABLE bool isPortFree(int port) const;
    // 自动分配一个空闲端口并写回 server.properties；used 为当前已占用的端口集合（运行中服务器）。
    // 返回新端口（失败返回 -1）。
    Q_INVOKABLE int assignFreePort(const QString &serverPath, const QSet<int> &used);
};
