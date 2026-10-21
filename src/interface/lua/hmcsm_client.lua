-- ============================================================================
-- HMCSM Lua 前端：WebUI REST API 客户端
-- ----------------------------------------------------------------------------
-- 这是「契约层」在 Lua 侧的体现：本文件只消费 WebUI 暴露的 JSON REST 接口，
-- 不关心后端是 Qt 还是 Rust——只要对方实现了同一份契约即可。
-- 依赖：LuaSocket（提供 Lua 5.1+ 的 socket.http / ltn12）。
--   Windows 可用 luajit + luasocket；或 luarocks install luasocket。
-- ============================================================================

local http = require("socket.http")
local ltn12 = require("ltn12")

local HmcsmClient = {}
HmcsmClient.__index = HmcsmClient

--- 构造客户端。
-- @param host 主机（默认 127.0.0.1）
-- @param port WebUI 端口（默认 25575）
-- @param token 访问令牌（可为 nil）
-- @param useTls 是否 HTTPS（默认 false）
function HmcsmClient.new(host, port, token, useTls)
    local proto = useTls and "https" or "http"
    host = host or "127.0.0.1"
    port = port or 25575
    local base = string.format("%s://%s:%s/api", proto, host, port)
    local self = setmetatable({}, HmcsmClient)
    self.base = base
    self.token = token
    return self
end

--- 内部：发起一次 GET/POST 并返回解码后的表（或 nil + 错误）。
function HmcsmClient:_request(method, path, body)
    local url = self.base .. path
    local respChunks = {}
    local headers = { ["Content-Type"] = "application/json" }
    if self.token then headers["Authorization"] = "Bearer " .. self.token end

    local payload
    if body then
        payload = (type(body) == "string") and body or require("json").encode(body)
    end

    local ok, code, _ = http.request{
        url = url,
        method = method,
        headers = headers,
        source = payload and ltn12.source.string(payload) or nil,
        sink = ltn12.sink.table(respChunks),
    }
    if not ok then
        return nil, "request failed: " .. tostring(code)
    end
    local raw = table.concat(respChunks)
    local data, err = require("json").decode(raw)
    if not data then
        return nil, "bad json: " .. tostring(err) .. " | " .. raw
    end
    return data, nil
end

--- 列出全部服务器及其运行状态。
function HmcsmClient:servers()
    return self:_request("GET", "/servers")
end

--- 读取系统资源占用。
function HmcsmClient:system()
    return self:_request("GET", "/system")
end

--- 启动服务器。
function HmcsmClient:start(name, path)
    return self:_request("POST", "/servers/" .. name .. "/start", { path = path })
end

--- 停止服务器。
function HmcsmClient:stop(name, path)
    return self:_request("POST", "/servers/" .. name .. "/stop", { path = path })
end

--- 强制停止。
function HmcsmClient:forceStop(name, path)
    return self:_request("POST", "/servers/" .. name .. "/forcestop", { path = path })
end

--- 发送一条控制台指令。
function HmcsmClient:send(name, path, cmd)
    return self:_request("POST", "/servers/" .. name .. "/send", { path = path, command = cmd })
end

--- 读取控制台缓冲。
function HmcsmClient:console(name, path)
    return self:_request("GET", "/servers/" .. name .. "/console?path=" .. (path or ""))
end

return HmcsmClient
