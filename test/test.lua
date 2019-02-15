local t, x, w = {}, {}, setmetatable({}, {__mode="kv"})
for i = 1, 3000000 do
    t[i] = i
    local p = newproxy()
    x = { { function() return p end }, ""..i }
    w[p] = x
end
