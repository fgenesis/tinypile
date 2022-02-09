#!/bin/env lua

-- Amalgamation script
-- Compatible with Lua 5.1 .. 5.4 and maybe more
-- Runs as part of the build process or can be run standalone

-- minilua doesn't have print(), tostring()
local print = print or function(...)
    io.stdout:write(table.concat({...}, '\t'), '\n')
end

setmetatable(_G, {
    __index    = function(_, k)    error("Attempt to get undefined global: " .. (k)) end,
    __newindex = function(_, k, v) error("Attempt to set global: " .. (k) .. " (to: " .. type(v) .. ")") end,
})

local function slash(s)
    if s:sub(-1) ~= "/" then
        s = s .. "/"
    end
    return s
end

local function trim(s)
    return (s:match("^%s*(.-)%s*$"))
end

local PATH = (...)
if not PATH then
    error("Usage: amalg.lua path/to/target")
end
local AMALGFILE = slash(PATH) .. "amalg.txt"

local INDIR = PATH
local FILE
local SKIP = {}

local function f_setdir(s)
    INDIR = slash(s)
end

local function f_put(s)
    FILE:write(s, "\n")
end

local function f_file(s)
    if FILE then
        FILE:close()
    end
    FILE = assert(io.open(s, "w"))
    print("Generate: " .. s)
end

local function f_skip(s)
    SKIP[s] = true
end

local function f_include(s)
    local fn = INDIR .. s
    print(".. " .. fn)
    for line in io.lines(fn) do
        if SKIP[trim(line)] then
            --line = "//-- " .. line
            line = nil
        end
        if line then
            FILE:write(line, "\n")
        end
    end
end

local CMDS =
{
    ["."] = f_setdir,
    [":"] = f_put,
    ["="] = f_file,
    ["<"] = f_include,
    ["-"] = f_skip,
    ["#"] = function() end,
}
setmetatable(CMDS, { __index = function(_, k) error("CMDS[" .. k .. "] unknown") end })

f_setdir(INDIR)
print("Amalgamating: " .. INDIR)
for line in io.lines(AMALGFILE) do
    local c, rest = line:match("^%s*(%S)%s*(.-)%s*$")
    if c then
        CMDS[c](rest)
    end
end
