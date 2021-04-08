local utf8 = assert(utf8) -- Requires Lua 5.3+

local function lowbias32(x)
    x = x ~ (x >> 15)
    x = (x * 0xd168aaad) & 0xffffffff
    x = x ~ (x >> 15)
    x = (x * 0xaf723597) & 0xffffffff
    x = x ~ (x >> 15)
    return x;
end

local HASHMASK = 0xff -- power-of-2 minus 1
local HASHSHIFT = 9
local function hash16(x) -- turns out this is ok as well
    return (x ~ (x >> HASHSHIFT)) & HASHMASK
end

local tins = table.insert
local tab = {}

for line in io.lines"CaseFolding.txt" do
    line = line:match"(.-)#"
    if line and line ~= "" then
        local c, ty, tmp = line:match"(%x+);%s(.-);%s([%x%s]+);"
        --print(c, ty, tmp)
        local cn = assert(tonumber(c, 16))
        if cn > 127 and (ty == "S" or ty == "C") then -- skip over the ASCII range and only do simple casefolding
            local t = {cn}
            for k in tmp:gmatch"%x+" do
                tins(t, assert(tonumber(k, 16)))
            end
            tins(tab, t)
        end
    end
end



local sm, big = 0, 0
local mapto = { {}, {}, {} }
local mapbig = {}
for _, t in ipairs(tab) do
    --print(table.unpack(t))
    --local seq = utf8.char(table.unpack(t, 3))
    --print(#seq)
    local n = #t
    local isbig
    for i = 2, n do
        if t[i] <= 0xffff then
            sm = sm + 1
        else
            big = big + 1
            isbig = true
            assert(n == 2)
            assert(t[i] <= 0x1ffff)
        end
    end
    tins(isbig and mapbig or mapto[n-1], t)
end
--print(sm, big)
--print(#mapto[1], #mapto[2], #mapto[3])

local function hex(i)
    return string.format("0x%x", i)
end

--[[local function dumpentry(e)
    local tmp = {}
    local n = #e
    for i = 1, n do
        tins(tmp, hex(e[i]))
    end
    return table.concat(tmp, ", "), n
end]]

local function dumpbucket(kout, vout, q)
    for _, e in ipairs(q) do
        tins(kout, e[1])
        for i = 2, #e do
            tins(vout, e[i])
        end
    end
end

local function dumptable(q)
    local B = setmetatable({}, { __index = function(t, k) local c = {} t[k] = c return c end })
    for _, t in pairs(q) do
        local hash = hash16(t[1]) & HASHMASK
        tins(B[hash], t)
    end
    local kout, vout, nums = {}, {}, {}
    for i = 0, HASHMASK do
        --table.sort(B[i], _sortbychar)
        local bs = #B[i]
        if bs > 0 then
            dumpbucket(kout, vout, B[i])
        end
       -- print("##", bs, table.unpack(B[i]))
        nums[i+1] = bs
    end
    return kout, vout, nums
end

local function fmtconcat(t, fmt, k, mask)
    local ret = {}
    local m = #t
    local j = 0
    while j < m do
        local tmp = {}
        for i = 1, k do
            local elem = t[j+i]
            if not elem then break end
            if mask  then
                elem = elem & mask
            end
            tmp[i] = fmt:format(elem)
        end
        tins(ret, table.concat(tmp, ", "))
        j = j + k
    end
    return table.concat(ret, ",\n")
end

local function _sortbychar(a, b)
    return a[1] < b[1]
end

local TYPEINFO =
{
    [1] = {name = "unsigned char", min = 0, max = 0xff},
    [2] = {name = "unsigned short", min = 0, max = 0xffff},
    [4] = {name = "unsigned int", min = 0, max = 0xffffffff},

}

local TOTALDUMP = 0

local function dumparray(a, bytesPerChar, name, fmt, perline, mask)
    local minval = TYPEINFO[bytesPerChar].min
    local maxval = TYPEINFO[bytesPerChar].max
    for i = 1, #a do
        local val = a[i]
        if mask then
            val = val & mask
        end
        if not (val >= minval and val <= maxval) then
            error("Value overflow, " .. a[i] .. " is not in [" .. minval .. " .. " .. maxval .. "]")
        end
    end
    print(("static const %s %s[%d] = {"):format(TYPEINFO[bytesPerChar].name, name, #a))
    print(fmtconcat(a, fmt, perline, mask))
    print("};");
    TOTALDUMP = TOTALDUMP + bytesPerChar * #a
end

local function genaccu(a)
    local accu = 0
    local cons = {}
    local n = #a
    for i = 1, #a do
        cons[i] = accu
        accu = accu + a[i]
    end
    cons[n+1] = accu
    return cons
end

print(("static inline unsigned casefold_tabindex(unsigned x) { return (x ^ (x >> %d)) & 0x%x; }"):format(HASHSHIFT, HASHMASK))

local mst = {}

for nc = 1, 3 do
    local kchr, vchr, nums = dumptable(mapto[nc])
    if next(kchr) then
        local ind = genaccu(nums)
        local nk, nv, ni =  "casefold16_" .. nc .. "_keys", "casefold16_" .. nc .. "_vals", "perbucket16_" .. nc .. "_accu"
        dumparray(kchr, 2, nk, "0x%04x", 16)
        dumparray(vchr, 2, nv, "0x%04x", 16)
        dumparray(ind, 2, ni, "%d", 32);
        tins(mst, { nk, nv, ni, nc, 0})
    end
end

do
    local kbig, vbig, nums = dumptable(mapbig)
    if next(kbig) then
        local ind = genaccu(nums)
        local nk, nv, ni = "casefold32_1_keys", "casefold32_1_vals", "perbucket32_1_accu"
        dumparray(kbig, 2, nk, "0x%04x", 16, 0xffff);
        dumparray(vbig, 2, nv, "0x%04x", 16, 0xffff);
        dumparray(ind, 2, ni, "%d", 32);
        tins(mst, { nk, nv, ni, 1, 0x10000 })
    end
end

for i, e in ipairs(mst) do
    mst[i] = (("{%s, %s, %s, %d, 0x%x}"):format(table.unpack(e)))
end
print("// Total size of referenced data: " .. TOTALDUMP .. " bytes")
print("static CasefoldData casefoldData[] = {")
print(table.concat(mst, ",\n"))
print("};")
