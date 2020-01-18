local function ssfunc()
    local test2 = require "test2"
    return test2
end

local function sfunc()
    ssfunc()
end

local function func(...)
    local arg = {...}
    -- for k, v in ipairs(arg) do
    --     print(k, v)
    -- end
    local idx = -1
    while true do
        local name, value = debug.getlocal(1, idx)
        if not name then break end
        print(name, value)
        idx = idx - 1
    end
    local c = 1
    local d = true
    local s = "abc"
    local t = {
        name = "time",
        age = 21,
        say = function()
            print("hello")
        end
    }
    print(t.name)
    sfunc()
    return c + d
end

local function main()
    pcall(func, 10, 20, "hello", true)
    local a = 1
    local b = 2
end

print("Hello world")
main()
