--[[
    调试器脚本
    by colin
]]
local cjson = require "cjson"
cjson.encode_empty_table_as_array(true)
local dbgaux = require "dbgaux"
local straux = require "straux"
local vscaux = require "vscaux"
local pathaux = require "pathaux"

-- 调试器运行状态
local ST_BIRTH = 0      -- 初始状态 
local ST_INITED = 1     -- 初始化完毕
local ST_RUNNING = 2    -- 运行状态
local ST_PAUSE = 3      -- 暂停状态：命中断点，或主动暂停
local ST_STEP_OVER = 4  -- 单步状态
local ST_STEP_IN = 5    -- 单步进入
local ST_STEP_OUT = 6   -- 单步跳出
local ST_TERMINATED = 10 -- 终止状态 

local THREAD_ID = 1     -- 线程ID

-- 调试器
debugger = {
    state = ST_BIRTH,   -- 状态
    currco = {},        -- 当前的协程信息
    coinfos = {},       -- 协程信息
    nodebug = false,    -- 不调试
    breakpoints = {},   -- 断点列表
    isattach = false,   -- 是否attach状态
    ispause = false,    -- 是否是主动暂停状态

    log = nil,          -- 测试代码
    obuffer = "",       -- 输出的缓冲
    osource = nil,      -- 输出的代码
    oline = nil,        -- 输出的行
}

-----------------------------------------------------------------------------------
-- 辅助函数

-- 打印错误日志
local function eprint(...)
    local ls = {...}
    for i = 1, #ls do
        ls[i] = tostring(ls[i])
    end
    local msg = table.concat(ls)
    io.stderr:write(msg)
    io.stderr:flush()
    debuglog(msg)
end

-- 取函数名
local function get_funcname(source, what, name)
    if what == 'Lua' then
        if name then return name end
    elseif what == 'main' then
        return '[chunk]'
    end
    return '[unknown]'
end

-- 过滤掉不能调试的代码，C代码，动态加载的代码
local lua_file_map = {}
local function check_call_filter(source, what)
    if debugger.nodebug then
        return false
    end
    if what == "Lua" then
        return true
    elseif what == "main" then
        source = straux.startswith(source, "@") and source:sub(2) or source
        local e = lua_file_map[source]
        if e == nil then
            e = not not pathaux.exists(source)
            lua_file_map[source] = e
        end
        return e
    end
    return false
end

-- 检查断点是否命中
local function breakpoints_hitest(source, line)
    source = straux.startswith(source, "@") and source:sub(2) or source
    local bps = debugger.breakpoints[source]
    if bps then
        for _, bp in ipairs(bps) do
            if bp.line == line then
                return true
            end
        end
    end
end

-----------------------------------------------------------------------------
-- 请求处理函数
local reqfuncs = {}
local seq = 1

function reqfuncs.initialize(coinfo, req)
    if debugger.state ~= ST_BIRTH then
        eprint("initialize- state error", debugger.state)
        return true
    end
    debugger.state = ST_INITED
    -- 回应初始化
    vscaux.send_response(req.command, req.seq, {
        supportsConfigurationDoneRequest = true,
    })
    -- 初始化完毕事件
    vscaux.send_event("initialized")
    -- 
    vscaux.send_event("output", {
        category = "console",
        output = "Lua Debugger started!",
    })
end

function reqfuncs.setBreakpoints(coinfo, req)
    if debugger.state == ST_BIRTH or debugger.state == ST_TERMINATED then
        eprint("setBreakpoints - state error", debugger.state)
        return true
    end
    -- 保存断点 和回应断点
    args = req.arguments
    local src = args.source.path
    local bps = {}
    for _, bp in ipairs(args.breakpoints) do
        bps[#bps+1] = {
            verified = true,
            source = {path = src},
            line = bp.line,
        }
    end
    debugger.breakpoints[src] = bps
    vscaux.send_response(req.command, req.seq, {
        breakpoints = bps,
    })
end

function reqfuncs.setExceptionBreakpoints(coinfo, req)
    vscaux.send_response(req.command, req.seq)
end

function reqfuncs.configurationDone(coinfo, req)
    if debugger.state == ST_BIRTH or debugger.state == ST_TERMINATED then
        eprint("setBreakpoints - state error", debugger.state)
        return true
    end
    vscaux.send_response(req.command, req.seq)
end

function reqfuncs.launch(coinfo, req)
    if debugger.state ~= ST_INITED then
        eprint("launch - state error", debugger.state)
        return true
    end
    -- noDebug
    debugger.nodebug = req.arguments.noDebug
    -- 设置lua path
    local luapath = req.arguments.luaPath
    if type(luapath) ~= 'string' then
        luapath = nil
    end
    local cpath = req.arguments.cPath
    if type(cpath) ~= 'string' then
        cpath = nil
    end
    if lupath or cpath then
        dbgaux.addpath(coinfo.co, luapath, cpath)
    end
    -- 运行脚本
    local program = req.arguments.program
    if not pathaux.exists(program) then
        vscaux.send_error_response(req.command, req.seq, "Launch failed: program not exists")
        vscaux.send_event("terminated")
        return true
    end
    local args = req.arguments.args or {}
    if type(args) ~= "table" then
        vscaux.send_error_response(req.command, req.seq, "Launch failed: args must be string array")
        vscaux.send_event("terminated")
        return true
    end
    for _, s in ipairs(args) do
        if type(s) ~= 'string' then
            vscaux.send_error_response(req.command, req.seq, "Launch failed: args must be string array")
            vscaux.send_event("terminated")
            return true
        end
    end
    -- 回应成功
    vscaux.send_response(req.command, req.seq)
    -- 运行脚本
    debugger.isattach = false
    debugger.state = req.arguments.stopOnEntry and ST_STEP_IN or ST_RUNNING
    -- TODO: run failed log
    dbgaux.runscript(program, args)
    -- 运行完毕
    vscaux.send_event("terminated")
    return true
end

function reqfuncs.attach(coinfo, req)
    if debugger.state ~= ST_INITED then
        eprint("attach - state error", debugger.state)
        return true
    end
    debugger.isattach = true
    debugger.state = req.arguments.stopOnEntry and ST_STEP_IN or ST_RUNNING
    vscaux.send_response(req.command, req.seq)
end

function reqfuncs.next(coinfo, req)
    debugger.state = ST_STEP_OVER
    coinfo.plevel = coinfo.level
    vscaux.send_response(req.command, req.seq)
    return true
end

function reqfuncs.stepIn(coinfo, req)
    debugger.state = ST_STEP_IN
    coinfo.plevel = coinfo.level
    vscaux.send_response(req.command, req.seq)
    return true
end

function reqfuncs.stepOut(coinfo, req)
    debugger.state = ST_STEP_OUT
    coinfo.plevel = coinfo.level
    vscaux.send_response(req.command, req.seq)
    return true
end

function reqfuncs.continue(coinfo, req)
    debugger.state = ST_RUNNING
    vscaux.send_response(req.command, req.seq)
    return true
end

function reqfuncs.threads(coinfo, req)
    vscaux.send_response(req.command, req.seq, {
        threads = {
            {id = THREAD_ID, name = "mainthread"},
        }
    })
end

function reqfuncs.pause(coinfo, req)
    debugger.state = ST_STEP_IN
    debugger.ispause = true
    coinfo.plevel = coinfo.level
    vscaux.send_response(req.command, req.seq)
end

function reqfuncs.stackTrace(coinfo, req)
    local levels = req.arguments.levels or 20
    local frames = dbgaux.getstackframes(coinfo.co, levels)
    vscaux.send_response(req.command, req.seq, {
        stackFrames = frames
    })
end

local function encode_varref(type, frameId)
    return (type * 100 + frameId) * 10000000
end

function reqfuncs.scopes(coinfo, req)
    dbgaux.startframe(coinfo.co);
    local frameId = req.arguments.frameId
    vscaux.send_response(req.command, req.seq, {
        scopes = {
            {
                name = "Arguments",
                variablesReference = encode_varref(1, frameId),
            },
            {
                name = "Locals",
                variablesReference = encode_varref(2, frameId),
            },
            {
                name = "Upvalues",
                variablesReference = encode_varref(3, frameId),
            },
        }
    })
end

function reqfuncs.variables(coinfo, req)
    local ok, vars = dbgaux.getvars(coinfo.co, req.arguments.variablesReference)
    if ok then
        vscaux.send_response(req.command, req.seq, {
            variables = vars
        })
    else 
        vscaux.send_error_response(req.command, req.seq, vars)
    end
end

function reqfuncs.disconnect(coinfo, req)
    vscaux.send_response(req.command, req.seq)
    dbgaux.exit()
end

-----------------------------------------------------------------------------------
-- 全局函数
function on_start()
    debuglog("on_start\n")
end

-- 开始hook一个线程
function on_new_thread(co)
    local coinfo = {
        co = co,        -- 协程
        level = 0,      -- 当前调用层级
        plevel = -1,    -- 停下来时的调用层级
    }
    debugger.coinfos[co] = coinfo
    if debugger.currco then
        debugger.currco = coinfo
    end
end

-- 停止hook一个线程
function on_free_thread(co)
    debugger.coinfos[co] = nil
end

-- 处理请求
function handle_request()
    while true do
        local req = vscaux.recv_request()
        if not req or not req.command then
            break
        end
        local func = reqfuncs[req.command]
        if func and func(debugger.currco, req) then
            break
        elseif not func then
            vscaux.send_error_response(req.command, req.seq, string.format("%s not yet implemented", req.command))
        end
    end
end

-- 调用HOOK
function on_call(co, source, what, name, line, level)
    local coinfo = debugger.coinfos[co]
    if coinfo then
        coinfo.level = level
    end
end

-- 返回HOOK
function on_return(co, source, what, name, line, level)
    local coinfo = debugger.coinfos[co]
    if coinfo then
        coinfo.level = level - 1
    end
end

-- 行HOOK
function on_line(co, source, what, name, line)
    local coinfo = debugger.coinfos[co]
    if not coinfo then return end
    if not check_call_filter(source, what) then return end

    local state = debugger.state
    -- 还未运行
    if state == ST_TERMINATED or state <= ST_INITED then
        return
    end

    -- 记住当前运行的协程
    debugger.currco = coinfo

    -- 判断断点是否命中
    if state == ST_RUNNING or state == ST_STEP_OVER or 
       state == ST_STEP_IN or state == ST_STEP_OUT then
        local reason;
        local hit = false
        if breakpoints_hitest(source, line) then     -- 断点命中测试总是在最前面
            reason = "breakpoint"
            hit = true
        elseif state == ST_STEP_OVER then
            if coinfo.plevel >= coinfo.level then
                reason = "step"
                hit = true
            end
        elseif state == ST_STEP_OUT then
            if coinfo.plevel > coinfo.level then
                reason = "step"
                hit = true
            end
        elseif state == ST_STEP_IN then
            reason = debugger.ispause and "pause" or "step"
            debugger.ispause = false
            hit = true
        end
        -- 命中
        if hit then
            debugger.state = ST_PAUSE
            coinfo.plevel = -1
            vscaux.send_event("stopped", {
                reason = reason,
                threadId = THREAD_ID,
            })
            print(string.format("%s:%s(%s)", source, line, get_funcname(source, what, name)))
        end
    end

    -- 命中后暂停，获得请求命令
    if debugger.state == ST_PAUSE then
        handle_request()
    end
end

-- 输出事件
function on_output(str, source, line)
    debugger.obuffer = debugger.obuffer .. str
    if source and source:sub(1, 1) == "@" then
        debugger.osource = source:sub(2)
    else
        debugger.osource = source
    end
    if not line or line < 0 then
        debugger.oline = nil
    else
        debugger.oline = line
    end
    if debugger.obuffer:sub(-1) == '\n' then
        vscaux.send_event("output", {
            category = "stdout",
            output = debugger.obuffer,
            source = debugger.osource,
            line = debugger.oline,
        })
        debugger.obuffer = ""
        debugger.osource = nil
        debugger.oline = nil
    end
end

function debuglog(msg)
    -- 正式版去掉下面的注释
    -- do return end
    if not debugger.log then
        debugger.log = io.open("/Users/colin/mylib/run.log", 'w')
    end 
    debugger.log:write(tostring(msg))
    debugger.log:flush()
end