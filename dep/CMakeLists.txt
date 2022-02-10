add_library(minilua minilua.c minilua.h)

set(libs "")
if(UNIX)
    list(APPEND libs m)
endif()

add_executable(runlua runlua.c)
target_link_libraries(runlua minilua ${libs})
