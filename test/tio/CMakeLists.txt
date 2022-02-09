add_executable(testtio0 testtio0.cpp)
add_executable(testtio1 testtio1.cpp)
add_executable(testtio2 testtio2.cpp)
add_executable(testvfsresolve testvfsresolve.cpp)

target_link_libraries(testtio0 tio)
target_link_libraries(testtio1 testdep tio)
target_link_libraries(testtio2 testdep tio tio_addons tio_zip tio_vfs)
target_link_libraries(testvfsresolve testdep tio tio_vfs)
