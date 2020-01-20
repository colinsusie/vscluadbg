--[[
    路径函数库
    by colin
]]
local lfs = require "lfs"

local pathaux = {}

function pathaux.exists(p)
    return lfs.attributes(p,'mode') ~= nil
end


return pathaux