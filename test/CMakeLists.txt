
include_directories(testdep)
add_subdirectory(testdep)

add_subdirectory(luaalloc)
add_subdirectory(jps)
add_subdirectory(tbsp)
add_subdirectory(tio)
add_subdirectory(sc)
add_subdirectory(tws)
add_subdirectory(tio_zip)

add_executable(tinymain tinymain.c)

if(MSVC)
    target_compile_options(tinymain PRIVATE /GS-)
    target_link_options(tinymain PRIVATE /NODEFAULTLIB )
else()
    target_compile_options(tinymain PRIVATE -fno-builtin -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables)
    target_link_options(tinymain PRIVATE -nostdlib -static)
endif()

target_link_libraries(tinymain sc)
hide_symbols(tinymain)
