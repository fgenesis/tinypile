cmake_minimum_required(VERSION 2.6)
project(tinypiletest)

add_executable(testluaalloc testluaalloc.cpp ../luaalloc.c ../luaalloc.h minilua.c)
