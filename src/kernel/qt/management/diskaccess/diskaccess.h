#pragma once

// ============================================================================
// Kernel / Management / 磁盘文件访问控制（Disk File Access Control）
// ----------------------------------------------------------------------------
// 全程序唯一的「路径真相」来源。历史上路径相关的 bug 几乎都源于各模块各自做字符串比较：
//   - QDir::absolutePath() 在 Windows 返回反斜杠，而 servers.json 存正斜杠 -> 一个服务器被认成多个；
//   - QML FileDialog 返回 file:///C:/x -> 盘符前多出 '/'；
//   - 仅比较字符串 -> 大小写差异导致同一目录被当作两个。
// 因此把 stripFileUrl / cleanPath / samePath / 根目录白名单 / 原子写 统一收敛到本类，
// 其余模块一律调用这里，禁止再自行拼字符串比较。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>

class DiskAccess : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList roots READ roots WRITE setRoots NOTIFY rootsChanged)
public:
    explicit DiskAccess(QObject *parent = nullptr);

    // ---- 静态：纯函数，无状态，可随处调用 ----

    // 剥离 file:/// 前缀并统一分隔符与 '.'/'..'，返回规范化的绝对路径。
    static QString normalizePath(const QString &path);

    // 大小写无关、分隔符无关的路径相等判定（必须用它替代 QString::operator==）
    static bool samePath(const QString &a, const QString &b);

    // 判断 child 是否位于 parent 之内（含自身）
    static bool isSubPathOf(const QString &parent, const QString &child);

    // ---- 实例：根目录白名单 ----

    // 允许内核读写的根目录集合（服务器目录 / 下载目录 / 临时目录）。
    // 白名单为空时视为「不限制」（保持与历史行为一致，避免升级后功能不可用）。
    QStringList roots() const { return m_roots; }
    void setRoots(const QStringList &roots);
    Q_INVOKABLE void addRoot(const QString &root);
    Q_INVOKABLE void removeRoot(const QString &root);

    // 是否允许访问该路径（白名单为空 -> true）
    Q_INVOKABLE bool isPathAllowed(const QString &path) const;

    // ---- 实例：文件操作（带访问控制与失败原因）----

    // 递归创建目录；受限路径或创建失败返回 false
    Q_INVOKABLE bool ensureDir(const QString &path);

    // 原子写入：先写临时文件再重命名，避免写入中途崩溃留下半个文件。
    // encoding 为 "utf-8"（默认）或 "utf-16"/"utf-16le"（server.properties 等部分文件需要）。
    Q_INVOKABLE bool writeFileAtomic(const QString &path, const QString &content,
                                     const QString &encoding = QStringLiteral("utf-8"));

    // 读取文本，失败返回空串
    Q_INVOKABLE QString readFile(const QString &path) const;

    // 递归删除（仅限白名单内，防止误删系统目录）
    Q_INVOKABLE bool removeRecursively(const QString &path);

    // 最近一次失败原因，便于 UI/日志提示
    Q_INVOKABLE QString lastError() const { return m_lastError; }

signals:
    void rootsChanged();

private:
    bool checkAllowed(const QString &path, const QString &op);

    QStringList m_roots;
    mutable QString m_lastError;
};
