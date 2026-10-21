#include "localconnection.h"

#include <QQmlContext>

LocalConnection::LocalConnection(Kernel *kernel, QObject *parent)
    : QObject(parent), m_kernel(kernel)
{
}

void LocalConnection::bindTo(QQmlContext *ctx)
{
    if (!ctx || !m_kernel)
        return;

    Kernel *k = m_kernel;

    // ---- Execution ----
    ctx->setContextProperty(QStringLiteral("serverManager"), k->serverManager());
    ctx->setContextProperty(QStringLiteral("systemMonitor"), k->systemMonitor());
    ctx->setContextProperty(QStringLiteral("downloadManager"), k->downloadManager());
    ctx->setContextProperty(QStringLiteral("javaManager"), k->javaManager());
    ctx->setContextProperty(QStringLiteral("downloadCatalog"), k->downloadCatalog());
    ctx->setContextProperty(QStringLiteral("installCoordinator"), k->installCoordinator());
    ctx->setContextProperty(QStringLiteral("createServer"), k->createServer());
    ctx->setContextProperty(QStringLiteral("importModpack"), k->importModpack());
    ctx->setContextProperty(QStringLiteral("serverController"), k->serverController());
    ctx->setContextProperty(QStringLiteral("settingsController"), k->settingsController());
    ctx->setContextProperty(QStringLiteral("appearance"), k->appearance());
    ctx->setContextProperty(QStringLiteral("portMapper"), k->portMapper());
    ctx->setContextProperty(QStringLiteral("backupController"), k->backupController());
    ctx->setContextProperty(QStringLiteral("schedulerController"), k->schedulerController());
    ctx->setContextProperty(QStringLiteral("updateController"), k->updateController());
    ctx->setContextProperty(QStringLiteral("proxyController"), k->proxyController());
    ctx->setContextProperty(QStringLiteral("proxyManager"), k->proxyManager());

    // ---- Control / Remote ----
    ctx->setContextProperty(QStringLiteral("webuiServer"), k->webuiServer());
    ctx->setContextProperty(QStringLiteral("botController"), k->botController());
    ctx->setContextProperty(QStringLiteral("remoteControl"), k->remoteControlServer());

    // ---- Management ----
    ctx->setContextProperty(QStringLiteral("diskAccess"), k->diskAccess());
    ctx->setContextProperty(QStringLiteral("processPrivilege"), k->processPrivilege());
    ctx->setContextProperty(QStringLiteral("resourceAllocator"), k->resourceAllocator());

    // 共用 QML 据此区分「配对(主机) / 连接(客户端)」段：本地端恒为 false
    ctx->setContextProperty(QStringLiteral("isRemoteEnd"), false);
}

QObject *LocalConnection::service(const QString &name) const
{
    return m_kernel ? m_kernel->bus()->service(name) : nullptr;
}

bool LocalConnection::hasService(const QString &name) const
{
    return m_kernel && m_kernel->bus()->hasService(name);
}

QStringList LocalConnection::serviceNames() const
{
    return m_kernel ? m_kernel->bus()->serviceNames() : QStringList();
}

QVariantMap LocalConnection::servicesSnapshot() const
{
    return m_kernel ? m_kernel->bus()->snapshot() : QVariantMap();
}
