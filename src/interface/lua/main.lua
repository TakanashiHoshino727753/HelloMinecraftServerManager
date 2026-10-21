-- ============================================================================
-- HMCSM Lua 前端入口（示例）：对接远端 WebUI REST API
-- 用法：
--   lua main.lua servers                # 列出服务器
--   lua main.lua system                 # 系统资源
--   lua main.lua start <名称> <路径>    # 启动
--   lua main.lua stop  <名称> <路径>    # 停止
--   lua main.lua send  <名称> <路径> <指令>   # 发送指令
-- 连接参数可用环境变量覆盖：HMCSM_HOST / HMCSM_PORT / HMCSM_TOKEN / HMCSM_TLS
-- ============================================================================

local client = require("hmcsm_client")

local host  = os.getenv("HMCSM_HOST") or "127.0.0.1"
local port  = tonumber(os.getenv("HMCSM_PORT") or "25575")
local token = os.getenv("HMCSM_TOKEN")
local tls   = os.getenv("HMCSM_TLS") == "1"

local c = client.new(host, port, token, tls)

local cmd = arg[1]
if cmd == "servers" then
    local data = c:servers()
    if data and data.servers then
        for _, s in ipairs(data.servers) do
            print(string.format("%-20s running=%-5s ver=%s", s.name or "?", tostring(s.running), s.version or "?"))
        end
    else
        print("no data")
    end
elseif cmd == "system" then
    local data = c:system()
    if data and data.system then
        for k, v in pairs(data.system) do print(k .. " = " .. tostring(v)) end
    end
elseif cmd == "start" then
    local _, err = c:start(arg[2], arg[3])
    print(err and ("error: " .. err) or ("start " .. tostring(arg[2])))
elseif cmd == "stop" then
    local _, err = c:stop(arg[2], arg[3])
    print(err and ("error: " .. err) or ("stop " .. tostring(arg[2])))
elseif cmd == "send" then
    local _, err = c:send(arg[2], arg[3], arg[4])
    print(err and ("error: " .. err) or "sent")
else
    print("usage: lua main.lua <servers|system|start|stop|send> ...")
end
