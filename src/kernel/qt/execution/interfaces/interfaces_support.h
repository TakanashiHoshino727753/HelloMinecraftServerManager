#pragma once
#include <QObject>

// ============================================================================
// Interfaces Support for other Managers（其它管理器的接口支撑）—— 占位模块
// ----------------------------------------------------------------------------
// 架构已规划但尚未实现：为「非 Minecraft 服务器类」的其它管理器（如
// 数据库 / 代理 / 通知中心自身）提供统一的接入与生命周期支撑，使其也能
// 经 ModuleBus 被接口层与其它模块同等对待。
// 当前仅占位，接口待补。
// ============================================================================
class InterfacesSupport : public QObject
{
    Q_OBJECT
public:
    explicit InterfacesSupport(QObject *parent = nullptr);
};
