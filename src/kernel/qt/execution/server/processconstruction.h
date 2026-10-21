#pragma once
#include <QString>
#include <QStringList>

// ============================================================================
// Process Construction（进程构造）
// ----------------------------------------------------------------------------
// 负责把一台服务端的启动参数「构造」出来：自适应探测不同加载器
// （NeoForge / Forge 1.17+ / Fabric / Forge 1.16- / 原版）的产物结构，
// 生成最终传给 `java` 的参数列表，并按需写入 user_jvm_args.txt。
// 不持有任何运行态——纯函数式，被 Server Control 在 start() 时调用。
// ============================================================================

class ProcessConstruction
{
public:
    // 构造启动参数（不含 java 本身）。ok=false 时 errMsg 说明失败原因
    // （如未找到任何可识别的服务端核心）。
    static QStringList buildLaunchArgs(const QString &serverPath,
                                       int minMem, int maxMem,
                                       bool *ok = nullptr,
                                       QString *errMsg = nullptr);

    // 在 dir 根目录查找首个文件名匹配 prefix 且不在 exclude glob 列表中的文件
    // （exclude 用于剔除安装器 jar / 日志）。仅扫描根目录。找不到返回空串。
    static QString findLaunchJar(const QString &dir, const QString &prefix,
                                 const QStringList &exclude);

    // 查找 NeoForge / Forge 1.17+ 的 args 文件（win_args.txt / unix_args.txt），
    // 返回相对 dir 的路径；优先根目录，否则递归扫描 libraries/。找不到返回空串。
    static QString findArgsFile(const QString &dir);
};
