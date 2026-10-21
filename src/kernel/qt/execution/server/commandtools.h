#pragma once
#include <QStringList>

// ============================================================================
// Command Tools（指令工具）：Instruction Set + Command Completion
// ----------------------------------------------------------------------------
// 服务端常用指令的指令集，以及基于前缀的指令补全（供 QML 输入框联想）。
// ============================================================================

class CommandTools
{
public:
    // 指令集：服务端常用指令（Instruction Set）
    static QStringList instructionSet();
    // 指令补全（Command Completion）：依前缀返回候选指令
    static QStringList complete(const QString &prefix);
};
