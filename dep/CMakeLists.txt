add_library(minilua minilua.c minilua.h)

add_executable(runlua runlua.c)
target_link_libraries(runlua minilua)
