#pragma once

// ============================================================================
// Kernel / Management / 进程权限管理（Process Privilege Management）
// ----------------------------------------------------------------------------
// 内核派生出大量子进程（Minecraft 服务端 java、Velocity 代理 java、NapCat、NoneBot、
// 打包用的 tar / PowerShell、openssl 等）。权限相关的事实与策略必须集中在一处：
//   - 事实：当前进程是否已提权、运行在哪个用户下、能否再提权；
//   - 策略：子进程是否继承提权状态、是否允许以提权身份启动。
// 本类只做「查询 + 记录策略」，不主动触发 UAC 弹窗：HMCSM 常规功能不需要管理员权限，
// 提权仅在用户明确要求（如绑定 80 端口）时才由界面层另行处理。
// ============================================================================

#include <QObject>
#include <QString>

class ProcessPrivilege : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool elevated READ isElevated NOTIFY privilegeChanged)
    Q_PROPERTY(bool canElevate READ canElevate CONSTANT)
    Q_PROPERTY(QString userName READ userName NOTIFY privilegeChanged)
    Q_PROPERTY(QString levelText READ levelText NOTIFY privilegeChanged)
    // 子进程权限策略：true = 子进程继承当前（可能已提权的）权限；false = 尽量以普通权限启动
    Q_PROPERTY(bool inheritElevation READ inheritElevation WRITE setInheritElevation
               NOTIFY inheritElevationChanged)
public:
    explicit ProcessPrivilege(QObject *parent = nullptr);

    // ---- 事实查询 ----
    bool isElevated() const;
    bool canElevate() const;
    QString userName() const;
    // 供 UI 直接显示的本地化文案：「管理员」/「标准用户」
    QString levelText() const;

    // ---- 策略 ----
    bool inheritElevation() const { return m_inheritElevation; }
    void setInheritElevation(bool on);

    // 进程启动前的统一判定：是否允许以当前策略派生子进程。
    // requireAdmin=true 且当前未提权时返回 false，调用方应改为提示用户，而不是静默失败。
    Q_INVOKABLE bool mayStartChild(bool requireAdmin = false) const;

    // 诊断/日志用：一行摘要（权限级别 + 用户 + 子进程继承策略）
    Q_INVOKABLE QString summary() const;

    // 重新检测（切换用户/权限变更后刷新缓存）
    Q_INVOKABLE void refresh();

signals:
    void privilegeChanged();
    void inheritElevationChanged();

private:
    void detect();

    bool m_elevated = false;
    bool m_canElevate = false;
    bool m_inheritElevation = true;
    QString m_userName;
};
