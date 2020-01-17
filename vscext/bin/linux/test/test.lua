local function ssfunc()
    local test2 = require "test2"
    return test2
end

local function sfunc()
    ssfunc()
end

local function func(a, b, ...)
    local c = a + 1
    local d = b + 1
    local s = "abc"
    local t = {
        name = "time",
        age = 21,
        say = function()
            print("hello")
        end
    }
    sfunc()
    return c + d
end

local function main()
    pcall(func, 10, 20, "hello", true, function()end)
    local a = 1
    local b = 2
end


-- local function hook(event)
--     local info = debug.getinfo(2, "Snlt")
--     print(info.source, info.what, info.name, info.namewhat, info.currentline)
-- end
-- debug.sethook(hook, 'c')


-- local function sfun()
-- end

-- function start()
--     sfun()
--     print(debug.traceback())
--     return 29
-- end

-- local s = [[
--     local a = 1
--     local b = 2
--     return a + b
-- ]]
-- local f = load(s, "@test")
-- f()
-- local test2 = require "test2"
-- start()

print(...)
-- main()
