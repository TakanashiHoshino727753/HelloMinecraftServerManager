#include "processprivilege.h"

#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lmcons.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

ProcessPrivilege::ProcessPrivilege(QObject *parent) : QObject(parent)
{
    detect();
}

void ProcessPrivilege::detect()
{
#ifdef Q_OS_WIN
    m_canElevate = true;   // Windows 有 UAC，理论上可以请求提权
    m_elevated = false;
    // 用进程令牌的 TokenElevation 判定是否已提权：
    // 不用 IsUserAnAdmin()——它来自 shell32/shlobj，需额外链接且在某些环境不可靠。
    // 取不到令牌信息时保守判定为「未提权」（宁可少报权限，也不要误判为管理员）。
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te = {0};
        DWORD size = sizeof(te);
        if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &size))
            m_elevated = (te.TokenIsElevated != 0);
        CloseHandle(token);
    }
    wchar_t name[UNLEN + 1] = {0};
    DWORD len = UNLEN + 1;
    if (GetUserNameW(name, &len))
        m_userName = QString::fromWCharArray(name);
    else
        m_userName = QString::fromLocal8Bit(qgetenv("USERNAME"));
#else
    m_canElevate = (geteuid() != 0);   // 非 root 时理论上可 sudo/su 提权
    m_elevated = (geteuid() == 0);
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
        m_userName = QString::fromLocal8Bit(pw->pw_name);
    else
        m_userName = QString::fromLocal8Bit(qgetenv("USER"));
#endif

    if (m_userName.isEmpty())
        m_userName = QStringLiteral("unknown");
}

void ProcessPrivilege::refresh()
{
    const bool wasElevated = m_elevated;
    const QString wasUser = m_userName;
    detect();
    if (wasElevated != m_elevated || wasUser != m_userName)
        emit privilegeChanged();
}

bool ProcessPrivilege::isElevated() const { return m_elevated; }
bool ProcessPrivilege::canElevate() const { return m_canElevate; }
QString ProcessPrivilege::userName() const { return m_userName; }

QString ProcessPrivilege::levelText() const
{
    return m_elevated ? QStringLiteral("管理员") : QStringLiteral("标准用户");
}

void ProcessPrivilege::setInheritElevation(bool on)
{
    if (m_inheritElevation == on)
        return;
    m_inheritElevation = on;
    emit inheritElevationChanged();
}

bool ProcessPrivilege::mayStartChild(bool requireAdmin) const
{
    // 常规子进程（java / tar / openssl 等）不需要管理员权限，始终允许。
    if (!requireAdmin)
        return true;
    // 需要管理员权限的子进程：只有当前进程已提权时才允许派生，
    // 未提权时返回 false，由界面层提示用户「以管理员身份重启 HMCSM」，而不是静默启动失败。
    return m_elevated;
}

QString ProcessPrivilege::summary() const
{
    return QStringLiteral("%1（%2，子进程继承:%3）")
        .arg(levelText(), m_userName, m_inheritElevation ? QStringLiteral("是") : QStringLiteral("否"));
}
