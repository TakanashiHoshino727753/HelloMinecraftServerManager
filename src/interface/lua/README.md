# HMCSM Lua 前端

Lua 实现的 **前端**（与 `src/interface/qt/` 平级，按语言分组）。它不编译进二进制，
由 Lua 解释器直接运行 `main.lua`，通过 **WebUI REST API**（`/api/servers`、`/api/system` 等）
对接任意后端——无论该后端是 Qt 还是 Rust，只要实现了同一份契约。

## 运行

```bash
luarocks install luasocket   # 提供 socket.http / ltn12 与 json（或另装 lua-json）
HMCSM_HOST=127.0.0.1 HMCSM_PORT=25575 HMCSM_TOKEN=xxxx lua main.lua servers
```

## 设计要点

- 本目录是「多语言前端」的示例：说明前端只认契约（REST JSON），与后端语言解耦；
- 与 Qt 前端的 `hmcsm_loader`（基于 C ABI / QLibrary）是同一契约的两种语言体现；
- 非 C++ 前端天然走 REST，无需为每个语言做 C ABI FFI 绑定。
