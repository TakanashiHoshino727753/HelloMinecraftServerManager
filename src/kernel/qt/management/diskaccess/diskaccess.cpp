#include "diskaccess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QRegularExpression>
#include <QTextStream>
#include <QtCore/qstringconverter.h>

namespace {
// 剥离 file:// 前缀（QML FileDialog / 拖拽 / 旧配置里都可能带），返回本地路径形态
QString stripFileUrl(const QString &path)
{
    QString p = path.trimmed();
    if (p.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive))
        p = p.mid(8);
    else if (p.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
        p = p.mid(7);
    return p;
}
} // namespace

DiskAccess::DiskAccess(QObject *parent) : QObject(parent) {}

QString DiskAccess::normalizePath(const QString &path)
{
    QString p = stripFileUrl(path);
    if (p.isEmpty())
        return QString();
    // Windows 上 QDir::absolutePath() 给的是反斜杠，而持久化配置里存正斜杠，必须先统一
    p = QDir::fromNativeSeparators(p);
    // cleanPath 同时能把 "/C:/x" 规范成 "C:/x"
    p = QDir::cleanPath(p);
    return p;
}

bool DiskAccess::samePath(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return normalizePath(a).compare(normalizePath(b), Qt::CaseInsensitive) == 0;
}

bool DiskAccess::isSubPathOf(const QString &parent, const QString &child)
{
    const QString p = normalizePath(parent);
    const QString c = normalizePath(child);
    if (p.isEmpty() || c.isEmpty())
        return false;
    if (p.compare(c, Qt::CaseInsensitive) == 0)
        return true;
    return c.startsWith(p + QStringLiteral("/"), Qt::CaseInsensitive);
}

void DiskAccess::setRoots(const QStringList &roots)
{
    QStringList normalized;
    for (const QString &r : roots) {
        const QString n = normalizePath(r);
        if (!n.isEmpty() && !normalized.contains(n))
            normalized.append(n);
    }
    if (normalized == m_roots)
        return;
    m_roots = normalized;
    emit rootsChanged();
}

void DiskAccess::addRoot(const QString &root)
{
    const QString n = normalizePath(root);
    if (n.isEmpty() || m_roots.contains(n))
        return;
    m_roots.append(n);
    emit rootsChanged();
}

void DiskAccess::removeRoot(const QString &root)
{
    const QString n = normalizePath(root);
    if (m_roots.removeOne(n))
        emit rootsChanged();
}

bool DiskAccess::isPathAllowed(const QString &path) const
{
    if (m_roots.isEmpty())
        return true;   // 未配置白名单 = 不限制（保持历史行为）
    const QString p = normalizePath(path);
    if (p.isEmpty())
        return false;
    for (const QString &r : m_roots) {
        if (isSubPathOf(r, p))
            return true;
    }
    return false;
}

bool DiskAccess::checkAllowed(const QString &path, const QString &op)
{
    if (isPathAllowed(path))
        return true;
    m_lastError = QStringLiteral("拒绝访问：%1 不在允许的根目录内（%2）").arg(path, op);
    return false;
}

bool DiskAccess::ensureDir(const QString &path)
{
    m_lastError.clear();
    const QString p = normalizePath(path);
    if (!checkAllowed(p, QStringLiteral("ensureDir")))
        return false;
    QDir dir(p);
    if (dir.exists())
        return true;
    if (!dir.mkpath(QStringLiteral("."))) {
        m_lastError = QStringLiteral("创建目录失败：%1").arg(p);
        return false;
    }
    return true;
}

bool DiskAccess::writeFileAtomic(const QString &path, const QString &content,
                                 const QString &encoding)
{
    m_lastError.clear();
    const QString p = normalizePath(path);
    if (!checkAllowed(p, QStringLiteral("write")))
        return false;

    const QDir dir = QFileInfo(p).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        m_lastError = QStringLiteral("创建父目录失败：%1").arg(dir.absolutePath());
        return false;
    }

    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("打开文件失败：%1（%2）").arg(p, f.errorString());
        return false;
    }

    const QString enc = encoding.trimmed().toLower();
    if (enc.startsWith(QStringLiteral("utf-16"))) {
        // server.properties / 部分 Mod 配置要求 UTF-16（LE），带 BOM 才能被正确识别
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf16);
        ts << content;
    } else {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << content;
    }
    if (!f.commit()) {
        m_lastError = QStringLiteral("提交写入失败：%1（%2）").arg(p, f.errorString());
        return false;
    }
    return true;
}

QString DiskAccess::readFile(const QString &path) const
{
    m_lastError.clear();
    const QString p = normalizePath(path);
    if (!isPathAllowed(p)) {
        m_lastError = QStringLiteral("拒绝访问：%1 不在允许的根目录内（read）").arg(p);
        return QString();
    }
    QFile file(p);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("读取失败：%1（%2）").arg(p, file.errorString());
        return QString();
    }
    const QByteArray raw = file.readAll();
    // 简单识别 BOM：UTF-16LE / UTF-16BE / UTF-8，其余按本地 8 位编码兜底
    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw.at(0));
        const unsigned char b1 = static_cast<unsigned char>(raw.at(1));
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
            QStringDecoder dec(QStringConverter::Utf16);
            return dec.decode(raw);
        }
    }
    QStringDecoder dec(QStringConverter::Utf8);
    QString out = dec.decode(raw);
    if (out.isEmpty() && !raw.isEmpty())
        out = QString::fromLocal8Bit(raw);   // 兜底：非 UTF-8 的老文件
    return out;
}

bool DiskAccess::removeRecursively(const QString &path)
{
    m_lastError.clear();
    const QString p = normalizePath(path);
    if (!checkAllowed(p, QStringLiteral("remove")))
        return false;
    QDir dir(p);
    if (!dir.exists())
        return true;
    if (!dir.removeRecursively()) {
        m_lastError = QStringLiteral("删除失败：%1").arg(p);
        return false;
    }
    return true;
}
