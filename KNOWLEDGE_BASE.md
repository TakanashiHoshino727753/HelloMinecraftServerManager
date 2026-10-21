# 参考知识库（KNOWLEDGE BASE）

> 用途：HMCSM（Minecraft Server Manager，Qt 6.11 + QML + C++，Windows / Linux）的**技术决策与排障速查**。
> 来源：2026-07 ~ 2026-09 的会话记录 + 已沉淀的长期记忆（详见 `CHANGELOG.md` 了解演进过程）。
> 注意：条目按"仍然有效"收录，但部分结论可能已被后续改动覆盖；改动前请以当前源码为准。

---

## 1. 构建与部署

- **本机 Qt 只有 Release 运行时**（无 `Qt6Cored.dll`），Debug 构建会在 `QApplication` 构造附近 SIGSEGV（`qtentrypoint_win.cpp`）。构建优先用 `MinSizeRel` / `RelWithDebInfo`。
- **编译前必须把 `D:\Developer\Git\mingw64\bin` 前置到 PATH**：mingw1310 的 `cc1plus.exe` 缺 libgmp/libmpfr/libmpc，任何真实编译会**静默失败**（不报错、不产出 .obj），`cmake --build` 只在缓存命中时"成功"。
- **改 QML 必须重编 exe**：应用 QML 编在 qrc / `qt_add_qml_module` 里，只拷贝 qml 文件无效；桌面端从文件系统加载时，改完源 `qml/` 要同步到实际运行目录（build 子目录 + 部署目录）。
- **新增 `.qml` 必须进 `QML_FILES`**：已改 `file(GLOB ... CONFIGURE_DEPENDS)`，但该选项只在**重新 cmake configure** 时重扫，只 `cmake --build` 不会纳入 → `xxx is not a type`。
- **发布包必须显式跑 `windeployqt`**（项目脚本原本 0 处引用）：补 Qt6 DLL **和** `Qt6/qml` 插件（缺插件 → `module not found` / `0xC0000135`）；产物常落在 build 子目录需与 exe 对齐；部署清单还要含 `OpenSSL 3.x dll`、`qopensslbackend.dll`、`openssl.exe`、`ZLIB1.dll`；部署时不要用 `/XF` 排除 exe。
- **"改了没生效"先怀疑跑的是旧 exe**：确认 exe 时间戳 → Clean 重建；exe 被 Defender 锁定时先 rename 成 `.old` 再复制。
- **打包 zip 平台分流**：Windows 优先 `tar -a`（bsdtar；`Compress-Archive` 对 `libraries/` 超深路径触发 260 字符限制并写出**损坏的 zip**），Linux 直接 `zip -r`；绝不能先试 `powershell`（其 start 失败只发 `errorOccurred` 不发 `finished`，后面的兜底永远不执行）。

## 2. 网络与下载

- **下载源按"国内可达"设计**：Vanilla / Forge / NeoForge / Fabric → **BMCLAPI**（`bmclapi2.bangbang93.com`；官方 `piston-data.mojang.com`、`maven.neoforged.net` 常被墙）；Modrinth → MCIM；Java → 清华 TUNA（→腾讯→官方）；PaperMC 必须 `fill.papermc.io/v3` 且**必须带 User-Agent**（v2 的 `api.papermc.io` 已废弃返回 410）；Adoptium 的 `latest` 会 302 到 GitHub，基本不可达。
- **必须自己加硬超时**：`setTransferTimeout` 不覆盖 TCP 连接阶段（被墙域名 SYN 黑洞会永久挂起），且 Qt6+MinGW 下 `reply->abort()` 不保证触发 `finished`。用 `QTimer` 到点直接 emit error 并丢弃任务，否则表现为"永远在解析下载地址 / 加载中"。
- **必须校验 HTTP 状态码**：`onFinished` 里读 `HttpStatusCodeAttribute`——403/5xx **不会**映射成 `QNetworkReply::error()`，Cloudflare 1010 拦截页会被当成功文件写盘（java 报 unexpected error）。installer jar 要校验 `PK` 头 + 最小体积；拿到 `text/html` 视为登录墙并换源。
- **统一入口**：重复的 `fetchJson`/`fetchText` 已收敛到 header-only `src/core/httpclient.h`，设 `setTransferTimeout` 兜底；HTTPS 首次请求存在 TLS 冷启动失败，需 1.5s 延迟重试（最多 3 次）。
- **TLS 后端**：`main.cpp` 设 `QT_TLS_BACKEND=openssl`；Windows 需把 Git for Windows 自带的 OpenSSL 3.x（`D:\Developer\Git\mingw64\bin\libcrypto-3-x64.dll`、`libssl-3-x64.dll`）拷到 exe 同目录（**Debug 目录同样要**）并带 `qopensslbackend.dll`；schannel 在 MinGW 下会偶发 `QIODevice::read (QSslSocket): device not open`，不可靠。TLS 自检代码必须在 `QApplication` 构造**之后**执行，否则污染 Qt 的后端缓存。
- **下载管理器规则**：`cancel()` 必须"先复制 Task → 先 erase → 再 abort"（否则二次 erase UAF）；`QSaveFile` 没有 `remove()`，丢弃用 `cancelWriting()` 且先判 `isOpen()`；续传用 `QFile` 追加（服务器不支持 Range 返回 200 则整文件重下）。

## 3. Java 管理

- **目录固定**：`HMCSM/jvm-{feature}/bin/java(.exe)`；`managedDir()` 委托 `DownloadManager::defaultDownloadDir()`，禁止再叠 `/jvm` 层或只用数字做目录名（会导致识别路径多一层、反复重下）。
- **Windows 解压用 PowerShell**：`Expand-Archive -LiteralPath <zip> -DestinationPath <dir> -Force`（141MB 约 2.3s）。**不要**用 `System.IO.Compression.ZipFile::ExtractToDirectory`（目标非空抛 IOException，造成"假失败"），也**不要**用系统 `tar.exe`（SysWOW64 重定向 + pax 头/符号链接卡死）。Linux 的 JDK 是 `.tar.xz`：`extractZip` 按 `PK` 头识别 zip，其余统一 `tar -xf`。
- **解压后必须单层展平**：若 dest 下只有一个含 `bin/java*` 的子目录（如 `jdk-25.0.4+7`），递归上移到 dest 根；目录覆盖不能用 `QFile::rename`（对目录 remove 失败），需递归 moveTree。否则 `installedJava` 找不到 java → 每次重新下载解压 141MB 死循环。
- **`waitForJavaReady` 不能用 `QFile::permissions()` 的 ExeUser/ExeGroup 位判断可执行性**（NTFS 上不可靠 → 永久阻塞 → 加载器永不运行），文件存在且后缀 `.exe` 即视为 ready。
- **版本映射**：`requiredFeature()` 由 MC 版本决定（≤1.16→8、1.20.1→17、1.21→21、新版 26.x→25），与加载器无关；版本字符串缺 `"1."` 前缀（如 `"21.9"`）会被误判成 8，需先补前缀再解析。JDK 8 只能走 Adoptium（Oracle 8 强制登录）；Java 11 用公开 `archive` 直链（`otn` 路径在 Qt6+MinGW+Schannel 下大文件 SIGSEGV）；需要免安装时用官方 package `.zip`。
- **架构自适应**：所有下载链接 / TUNA 路径 / Oracle 兜底 / 注册键都要用 `JavaManager::hostArchitecture()`（硬编码 x64 在 arm64 Linux → `UnsatisfiedLinkError`）。
- **复用校验**：`ensureTemp` 复用旧解压目录前必须 `probeJava`；`installBase` 用完复位，避免污染其他调用。
- **临时 Java**：创建/打包用的临时 Java 与临时目录放 `QStandardPaths::TempLocation`（恒为纯 ASCII），打包完再清理。

## 4. 服务器管理与进程

- **身份键必须用 `path` 而不是 `name`**：自动命名（如 `NeoForge-26.2`）会重名，导致控制台/状态/指令串台；每个服务器目录写 `.hmcsm/id` 作持久标识。
- **路径比较一律 `samePath`**（`QDir::cleanPath` + `toLower`）：`QDir::absolutePath()` 返回反斜杠而 `servers.json` 存正斜杠，直接字符串比较会把一个服务器认成多个。
- **扫描**：只扫 `defaultServerDir()`（绝不递归整个"我的文档"）；跳过祖先目录已含 `server.properties` 的嵌套目录（backups/worlds/临时解包会被误认成独立服务器）；扫描后 `dedupeByPath`；"刷新"必须移除磁盘已不存在的条目并无论如何 emit `serversChanged`。
- **目录布局**：全平台统一 `Downloads/HMCSM`（Windows `Downloads\HMCSM`，Linux 锁死 `~/HMCSM/Downloads` 与 `~/HMCSM/Servers`，不依赖 QStandardPaths 以避开中文"文档"路径）。
- **路径规范化**：`stripFileUrl()` + `fromNativeSeparators` + `cleanPath`（`cleanPath` 能把 `/C:/x` 规范成 `C:/x`）；注册表/QSettings 里可能残留脏值，**读取时也要剥离**。
- **进程**：`waitForStarted` 超时给 **30s**（Defender 首次扫描新下载的 `jvm-*/bin/java.exe` 常超 10s，是"首次创建失败、重试就成功"的根因）；`QProcess` 必须同时连 `errorOccurred` 并检查 `start()` 结果（只连 `finished` 会在启动失败时静默死等）；超时 guard 的 `QTimer` 声明必须在 `start()` 之前且显式 `start()`。
- **Linux 中文路径**：无 UTF-8 locale 时 JVM 会把路径解成 `??????`（Qt 会 fallback C.UTF-8 但 JVM 不认）→ 给派生的 java 进程注入 `LC_ALL=C.UTF-8`，临时目录走 `TempLocation`。
- **CPU / 内存**：Windows `FILETIME` 是 100ns 单位，必须 `/10000` 转 ms 再比墙钟（漏除会放大万倍出现 7805%），多核总和 >100% 正常；Linux 需单独实现 `/proc/<pid>/stat` 与 `VmRSS`（原逻辑被 `#ifdef Q_OS_WIN` 包住导致 Linux 恒 0）。
- **退出**：`stopAll()` 直接 `kill()`（不是 terminate）避免孤儿 java；托盘菜单里直接 quit 不可靠（菜单嵌套事件循环），应在 `aboutToHide` 之后退出 + 500ms 兜底 Timer + `m_quitting` 防重入。

## 5. 模组服（Forge / NeoForge / Fabric）

- **启动方式按加载器自适应**，不能永远 `java -jar server.jar`：
  - NeoForge / Forge 1.17+：`java @user_jvm_args.txt @unix_args.txt`（Windows 为 `win_args.txt`；26.x 时它在 `libraries/net/neoforged/.../` 深层，必须**递归查找**）。
  - Fabric：入口是 `fabric-server-launch.jar`（根目录 `server.jar` 只是原版核心备份）。
- **清理产物时禁止删** `unix_args.txt` / `win_args.txt` / `user_jvm_args.txt` / `version.json`；也不要把 `neoforge-*-server.jar` 复制成 `server.jar` 后删原 jar（args 文件里的 `-classpath`/`-jar` 用相对原路径，删了就起不来）。
- **NeoForge 26.x** 安装器不生成运行时核心 `neoforge-{ver}.jar`（maven 只有 `-universal.jar`），需自行把同目录 universal 复制为目标名；版本选择优先稳定版，全为预发布时选**最低**的 beta。
- **并发模型**：安装器**下载可并行、运行必须串行**（共享单实例 `m_installer` / `m_activeLoader` / `m_installerLog`），且必须等 Java 解压就绪（`m_javaReady`）；用统一 `pumpInstaller()` 驱动 `m_pendingInstall` 队列，任何失败分支都要复位 `m_installing` 并 pump，否则队列永久卡死。
- **重试计数**不要绑定 `useMirror` 参数（职责混叠会双重递增/漏增）；`start()` 被自动重启拉起时会清零计数，需区分手动/自动（`resetRetry`）；`waitForStarted` 超时这类可重试失败也应计入。
- **双入口产物一致**：桌面/机器人走 `InstallCoordinator`、WebUI 走 `CreateServerController`，两者都要 `setPackaged(true)` + `setSkipAddList`，setter 顺序固定：**`setPackaged` → `setSaveDir`(完整路径) → 最后 `setName`**（`setName` 会触发 `refreshSaveDir`，saveDir 未设会回退 `defaultServerDir()` 把产物错落到 Documents）。
- **打包数据源用构建目录 `m_modsTemp`**，不能用 `m_saveDir`（可能就是程序目录，会把 exe 和 logs 打进包）；packageOnly 模式既不复制进 `m_saveDir` 也不删除它。
- **args 分支**：在 `user_jvm_args.txt` 注入 `-Djava.awt.headless=true`，且不要另传 `-Xms/-Xmx`（文件内后展开的值会覆盖命令行）。

## 6. 代理（Velocity）

- 需要 **Java 17+**（取后端 `.hmcsm/java.txt` 里第一台找到的 JDK）；启动代理自动生成 `velocity.toml` + `forwarding.secret`，并把后端 `server.properties` 改为 `online-mode=false`（Paper 另写 config 启用 modern forwarding）。
- 非 Paper 后端自动降级 `none` 转发是设计如此，但必须保持后端 `online-mode=false`。
- 实测：**后端服务器先启动、再开代理**才能连上。
- 绑定改复选框 + 解绑确认；`stop()` 只发 `shutdown` 会长时间仍显示运行中 → 发完命令立即 `setStatus("正在停止…")` + emit `statusChanged`，强杀超时由 8s 降到 3s。

## 7. QML / UI

- **编译期**：
  - Qt 6.11 qmlsc：对象声明后跟 `};` 是**非法语法**（直接 failed to load component 并退出进程）；分组属性 `anchors {...};` 同理。
  - `qt_add_qml_module` 的源码属性（`QT_QML_SINGLETON_TYPE`、`QT_QML_SKIP_QMLDIR_ENTRY`）必须写在**调用之前**，否则不生效（Theme 全 undefined）。
  - 自定义信号必须用 `onXxx` 处理器，不能 `signalName: function()`；无版本号 `import QtQuick.Controls` 才能把 `Dialog` 解析为具体控件（带 2.15 会解析到抽象基类）。
  - `TabView`/`Tab` 在该构建下解析失败 → 用 `TabBar` + `StackLayout`。
- **API 细节**：`FileDialog` 用 `selectedFile`（Qt6 改名，`.file` 返回空）；`Image.source` 必须是 URL（Windows 绝对路径会被当协议报 `Protocol "d" is unknown`）→ C++ 侧 `QUrl::fromLocalFile(path).toString()`；`delegate` 必须挂在 ComboBox 自身（`popup` 是 Popup 没有该属性，写错整页加载不出）；editable ComboBox 直接赋 `editText` 不生效，要把值加进 model 再设 `currentIndex`；`TextField` 必须显式设 `color`（只设 `palette.text` 深色主题不可见）。
- **C++ ↔ QML 契约**：`Q_PROPERTY` 当值读（加 `()` 报错）；要当函数调用的 setter 必须 `Q_INVOKABLE` 且放在 public 区；`Q_PROPERTY` 宏不能写在 `public slots:` 内（moc 解析失败）。
- **可见性/动画**：`window.show()` 是异步的，同栈里 `fadeIn.start()` 会让 opacity 卡 0（只有任务栏图标/黑屏）→ 统一 `onVisibleChanged: if (visible) fadeIn.start()` 并留 `opacity: 1` 兜底；`opacity` 是**乘性**的（根 Rectangle 设 0.15 会让整棵子树变淡），z 相同则后声明在上。
- **圆角**：`clip: true` 只按矩形裁剪、`radius` **不裁剪子项** → 真圆角需 `layer.enabled: true`（离屏纹理）或 `OpacityMask`；开启背景图后丢圆角就是图片未做圆角处理。
- **布局**：`Flickable`/`ListView`/`RowLayout`/`StackLayout`/`Flow` 没有 `padding`/`background`，也不支持 `Layout.*` 附加属性；`StackLayout` 子项用 `Layout.fillWidth/fillHeight/margins`，用 `anchors` 会被布局接管而失效；`Layout.preferredHeight` 只是"首选"，实际下限由子项 `implicitHeight` 决定；`ColumnLayout` 中要放一个始终 `Layout.fillHeight: true` 的容器吸收剩余空间。
- **易错点**：`SpinBox` 写 `value:` + `onValueChanged` 回写会形成绑定环（只初始化用 `Component.onCompleted`）；嵌套子对象内的函数/属性**不会**提升到父作用域；`ListModel` 在 `Repeater` 里 `modelData` 为 undefined 必须用角色名；delegate 里访问字段要加空守卫（空 model 每次按键刷几十条 TypeError 会拖垮 UI）；判定用户交互用 `dragging` 而非 `moving`；开关一律用原生 `Switch`（自绘的无法点击）。
- **运行态同步**：不能只靠 `stateChanged` 驱动（窗口后打开时信号早已发过）→ `Component.onCompleted` 主动同步 + 2s 定时校正；控制台日志要 80ms 节流批量 append，且仅在用户停底部时滚动。

## 8. WebUI

- **不用 `QHttpServer`**：`QHttpServer::bind(QTcpServer*)` 在 Qt 6.11 + MinGW 下**运行期直接崩主进程**；内嵌 HTTP 一律 `QTcpServer` + 手写解析（`WebUIServer`）。要 TLS 必须子类化 `QTcpServer` 重写 `incomingConnection(qintptr)` 直接建 `QSslSocket`，**不能**用 `nextPendingConnection()` 取 `QTcpSocket` 再 `setSocketDescriptor`（两对象共享描述符抢读事件 → 握手卡死）。`WebTcpServer` 必须真的被实例化（否则端口在听但连接走空实现）。HTTPS 分支要在 `startServerEncryption()` **之前**连好 `readyRead`。
- **自签证书**：全平台统一用 **`openssl req`** 命令生成 PEM（CryptoAPI / `CryptExportPKCS8` 方案在部分机器上触发 CRYPT32 访问违规 0xc0000005，整个 HMCSM 闪退）；部署需带 `openssl.exe` + `ZLIB1.dll`，并给 QProcess 设 `OPENSSL_CONF=NUL`（Windows）/ `/dev/null`（Linux），否则 openssl 去找硬编码的 openssl.cnf 而失败。降级明文时必须用 `QTcpServer`（`QSslServer` 没证书会强制握手 → 连接被关闭）。
- **API 约定**：端点统一 `/api/*` 且必须带令牌（否则 401）；`webuiEnabled` / `webuiPort` 等有害设置不暴露；`webuiExposeLan=false` 只绑 127.0.0.1 会导致外部访问不了（Linux 默认改为 true 绑 0.0.0.0 + 放行防火墙）。所有错误（401/404/400）统一返回 JSON（不要 text/plain）。
- **进度单位**：`InstallCoordinator::tasks()` 对外输出 0–1（内部 0-100 需 `/100.0`），否则前端 `*100` 会出现 7000%；前端取 `res.tasks`（对象不是数组），POST body 必须 `JSON.stringify`，任务全部 finished 后停止轮询。
- **二维码**：库必须**内联进 SPA 的 `<head>`**（占位符替换为 `QRCODE_JS`），独立 `<script src="/qrcode.js">` 子资源会与 TLS 握手竞态丢失；`QJSEngine` 里库与生成逻辑必须拼成单次 `evaluate`；网上下载的 JS 可能是 UTF-16LE with BOM，写进 raw string 前要核对编码。

## 9. 机器人（NapCat / NoneBot）

- **控制通道独立于任一 bot 进程**：`startControlServer()` 不能只在 `startNonebot()` 里调用（只用 NapCat 时 25585 根本没起）→ 改为 `updateControlServer()` 按 `m_napcat || m_nonebot` 启停，两个都关才停。
- **NoneBot（2.5.0）**：`on_message` 是 Matcher **类**（非工厂），`@on_message()` 会抛参数错误 → `m = on_message(); m.handle(handler)`；`@driver.on_startup` 在本环境**不触发**，后台任务改用独立守护线程自带 `asyncio` 事件循环；WS 客户端必须 `ping_interval=None, ping_timeout=None`（HMCSM 的 25585 不响应 ping），靠 HTTP `/api/ping` 心跳；真实运行身份是 venv python（`nb run` re-exec）。
- **插件 Python 只用标准库 `urllib`**，不要依赖 `httpx` 等需联网安装的包（离线 venv 装不上会导致插件加载失败）。
- **NapCat 插件**：`package.json` 必须含 `"napcat": {"type":"extension", ...}`；HMCSM 必须自带构建好的 `dist`（`index.mjs` + `package.json`）；`startNapcat` 前先 `ensureNapcatPlugin`；"自动启用"除 `napcat.mjs` 白名单外还必须在 `napcat.json` 的 `plugins` 列表登记且 `disabled:false`；回调签名是 `(ctx, config)`（从实参里挑含 `hmcsm_*` 键的对象），配置双写（`ctx.configPath` + 插件目录 `config.json`），合并顺序 默认值 < 本地文件 < configPath < 注入配置。
- **令牌轮换**：移除 loopback 豁免后，令牌重生成必须回写两个插件配置并用 pending 标志在进程退出后自动重启运行中的 bot，否则持续 401；`dispatchApi` 必须把解析好的 `hdr` 传进 `ctrlAuthorized`（传空会忽略 `Authorization: Bearer`）。
- **关闭崩溃点**：`stopControlServer` 里 `delete m_tcp` 连带销毁子 socket 后又 `deleteLater` 会双重释放（只在"控制服务在跑 + 有 WS 客户端时关闭"触发）→ 重启编排要避开或加待重启标志走进程退出回调。
- **VM 部署**（192.168.138.130）：部署前先 Stop+Disable 遗留 nssm 服务（全局 Python 的独立 bot 会占 8080）；WinRM 网络登录写入的 `HKCU` 与桌面会话不同配置单元（QSettings 全键读不到），机器级默认值应写 `HKLM`。

## 10. 远控端（HMCSM_Remote）

- **外观个性化是各端本地的**：背景图/BGM/标题栏/透明度属于客户端本地 `AppearanceSettings`（各端独立 QSettings、皮肤目录 `HMCSM_Remote/skin`），**绝不**代理到被控端 `settingsController`（被控端的壁纸文件不在远端机器上）；`merge()` 刷新时跳过外观字段。
- **配对方向**：现有"配对"是 WebUI 专属、一次性、绑定 `webuiToken`（令牌轮换即失效）。正确做法是本地端新增**独立于 WebUI 的 RemoteControlServer + 长期设备凭证配对**；远端反代必须用 `https` 且配 `QSslConfiguration`（`VerifyNone` + `TlsV1_2OrLater`）忽略自签证书，否则对已在跑 HTTPS 的 WebUI 用 http 反代会 502。
- **工程**：`main.cpp` 需 `SKIP_AUTOMOC`（避免 moc 重复定义导致链接多重定义）；必须 `QQuickStyle::setStyle("Basic")`（原生样式拒绝自定义 `background`，照搬的桌面 UI 渲染不出）；`shared` 拆为 `hmcsm_logging`（仅 Qt Core）与 `hmcsm_appearance`（Core+Multimedia）。
- **网络对象生命周期**：对端 socket 用 `QPointer<QTcpSocket>` 捕获，远控先退出时自动置空，避免回复时 SIGSEGV。

## 11. 架构与工程约定

- **分层单向无环**：`hmcsm_serverctl` → `hmcsm_download` → 更底层；反向依赖会成环（跨层复用在应用层 `main.cpp` 加协调器，如 `InstallCoordinator` 放 `hmcsm_serverctl` 层）。`DownloadManager` 是**纯头文件内联实现**（无 .cpp），只 include 头不会引入新链接依赖；类静态成员（`static int s_seq;`）没有 .cpp 会链接失败 → 改函数内静态。
- **源码树（2026-10 起按 Interface / Kernel 分层）**：`src/interface`（gui / cli / tli / remote）为前端，`src/kernel` 分 `control`（local / remote{webui,pairing,napcat}）、`management`（modulebus / diskaccess / processprivilege / resourceallocation）、`execution`（download / java / server / serverctl / settings / monitor / ops / proxy / notify）；`src/shared` 为两端共用（logging / appearance）。依赖方向单向：`Interface → Control → Execution → Management`，反向依赖属架构违规。
- **组合根**：`src/kernel/control/local/kernel.cpp` 是唯一装配点（构造全部服务 + 跨模块接线 + 注册进 ModuleBus）；`main.cpp` 只做进程级初始化（日志/TLS/QApplication/主题/QML 引擎）。新增业务对象一律在 `Kernel` 里构造，不要让 `main.cpp` 重新膨胀。
- **模块取用**：模块之间经 `ModuleBus`（`registerService` / `service(name)` / `publish/subscribe`）互相发现，不要互相 `#include` 内部头文件；接口层经 `LocalConnection` 取用，QML 上下文属性名与 ModuleBus 注册名保持一致。
- **`execution/server/` 严格按功能节点拆分**：`ServerController` 只是门面（聚合+转发+信号透传），真正逻辑落在子模块——`ServerControl`(进程运行时) / `ServerConfig`(配置文件管理) / `ProcessConstruction`(启动参数自适应探测) / `ServerInfo`(CPU/内存采样) / `WatchDog`(卡死双检测) / `AutoCorrection`(异常纠错中心) / `CommandTools`(指令集+补全) / `PlayerInfo`(玩家信息获取/组织) / `WorldInfo`(世界信息/备份恢复) / `ModsManager`(模组支持检测/冲突排除)。进程运行态(`Proc`)只归 `ServerControl` 所有；看门狗/纠错中心经其暴露的访问器观察与命令进程，不直接持有 `QProcess`。占位模块 `database`/`ai`/`interfaces`（hmcsm_database/hmcsm_ai/hmcsm_interfaces）目前为空壳，待补。
- **跨模块 include 一律用裸文件名**（如 `#include "proxymanager.h"`），由 CMake 的 `target_include_directories` 提供搜索路径；不要写 `../xxx/yyy.h` 相对路径（会随目录调整失效，也会掩盖真实依赖）。
- **i18n**：`qt_add_qml_module` 会把 `qsTr("中文")` 注册进模块内建翻译表并**优先于** `QCoreApplication::translate()`（自定义 Translator 装了也不被调用）；切换语言需销毁重建窗口 + `engine->clearComponentCache()`；`I18n.qml` 运行时用内联字典（`app_en.json` 从未加载）→ 翻译要同步进内联字典。托盘菜单也要翻译。
- **配置存储统一用默认 `QSettings()`**（`main.cpp` 已设 org=`HMCSM`、appName=`MinecraftServerManager`）；显式 `QSettings("HMCSM","HMCSM")` 会落到另一个注册表/ini 位置，造成设置漂移。
- **开机自启**：Win32 写 `HKCU\...\Run`（`HKLM` 普通用户只读，且绕开 QSettings 路径歧义）；命令行含空格必须加引号；读回比对时先去引号再用 `canonicalFilePath()` 统一分隔符与大小写。
- **对象生命周期**：所有控制器声明在 `QQmlApplicationEngine` **之前**，`AppController` 作为 engine 子对象，否则退出时 QML 批量报 `Cannot read property 'xxx' of null`；跨线程改设置/启停用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 或 `QTimer::singleShot(0,...)` 切回主线程（否则 `stateChanged` 从工作线程发出、QML 收不到）。
- **依赖显式注入并断言**：不要靠 `qobject_cast<DownloadManager*>(parent)` 偷取（父对象没传静默变 nullptr）。
- **异步回调三件套**：递归 lambda 用 `std::shared_ptr<std::function<...>>` 持有（按值捕获会捕获空对象 → `bad_function_call` / SIGSEGV）；回调入口用 `QPointer` 守卫 `if (!self) return;`；按值捕获的指针不能在 lambda 内赋值。
- **日志**：Debug 构建输出到控制台 + debug 日志文件，Release 仅写正常日志文件；固定 `<程序目录>/logs/hmcsm.log`（首次写入 mkpath），行格式 `时间戳 + DBG/INF/WRN/CRT/FTL + category + 消息`；超过 5MB 轮转为 `hmcsm.log.1`；诊断模式 `--console` / `-c` / `-diag` 同时打 stderr 与写文件。
- **托盘**：Linux 直接用原生 `QSystemTrayIcon`（不要自制 FakeTray 浮窗）；仅当 `isSystemTrayAvailable()` 时程序常驻（关窗=收起托盘），否则关窗即退出；`QSystemTrayIcon` 不能是 `if` 块内局部变量（块结束即析构 → 托盘消失）。

## 12. 排障速查（症状 → 原因）

| 症状 | 常见原因 |
|---|---|
| 改了 QML 没生效 | 跑的是旧 exe（需重编 / Clean 重建 / 同步到运行目录）；新 qml 未重新 cmake configure |
| 主窗口打不开、只有托盘图标 | 缺 windeployqt 部署的 QML 插件；QML 组件加载失败（`};` 语法、未注册类型） |
| "永远在解析下载地址 / 加载中" | 无硬超时（SYN 黑洞）；源不可达需换镜像；`setTransferTimeout` 不覆盖连接阶段 |
| 下载完报 unexpected error | 403/5xx 未校验，把 Cloudflare 错误页当文件写盘（未校验 HTTP 状态码） |
| 模组服装完起不来 | args 文件被删 / 启动方式未按加载器切换（应 `@args.txt`、Fabric 用 `fabric-server-launch.jar`） |
| Java 反复重下 141MB | 解压未单层展平 → 找不到 java；或 `permissions()` 判可执行失败 |
| 一个服务器被认成多个 | 路径未 `samePath` 规范化；扫描未跳过嵌套目录 |
| CPU 占用 7805% | FILETIME 100ns 未 `/10000` 换算 |
| Linux 中文路径变 `??????` | 未给 java 进程注入 `LC_ALL=C.UTF-8` |
| 一开 WebUI 就闪退 | CryptoAPI 自签证书 SIGSEGV（改用 `openssl req`） |
| WebUI 连不上 / 502 | 只绑 127.0.0.1；远端反代用 http 访问已在跑 HTTPS 的 WebUI |
| 退出后主程序不退出 | 托盘菜单内直接 quit（应在 `aboutToHide` 后退出）；java 子进程未 kill |
| 构建"成功"但没产物 | cc1plus 缺数学库 → PATH 未前置 Git mingw64；或缓存命中未真编译 |
| 编译出的 exe 启动 SIGSEGV | 用 Debug 构建（本机 Qt 无 Debug 运行时）→ 改用 MinSizeRel |

## 13. 协作偏好

- **git 提交信息用纯 ASCII 英文**，或用「UTF-8 文件 + `git commit -F`」（Windows 下 `git -m` 传中文会双重转码成乱码）；**不自动提交/推送**，需明确同意；不重写历史、不 force push；推送同时到 GitHub 与 Gitee。
- **改动最小化**："不要改其他代码"，只做冲突/崩溃修复；顺手做的"优化/告警消除"要能按要求还原。
- **反对无谓合并**：napcat 就是 napcat、Windows 远控就是 Windows 远控。
- **README 维护**：要求持续更新"全部功能 + 未完成功能 + 待修复 bug"。
- 回复与文档用简体中文。
