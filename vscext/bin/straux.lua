--[[
    字符串辅助函数
    by colin
]]
local straux = {}

-- 判断字符串是否始于ss
function straux.startswith(s, ss)
    return string.find(s, ss, 1, true) == 1
end

return straux