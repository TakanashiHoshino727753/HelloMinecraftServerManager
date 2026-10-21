#pragma once
#include <QObject>

// ============================================================================
// Database Support（数据持久化）—— 占位模块
// ----------------------------------------------------------------------------
// 架构已规划但尚未实现：服务端元数据 / 运行历史 / 备份清单等的本地持久化
// （SQLite 或嵌入式数据库），替代当前散落的 QSettings / JSON 文件。
// 当前仅占位，接口待补；实现后在此接入 ModuleBus 供其它模块按名调用。
// ============================================================================
class DatabaseSupport : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseSupport(QObject *parent = nullptr);
};
