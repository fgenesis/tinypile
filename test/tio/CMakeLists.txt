add_executable(testtio1 testtio1.cpp)
add_executable(testtio2 testtio2.cpp)
add_executable(testvfsresolve testvfsresolve.cpp)

target_link_libraries(testtio1 testdep)
target_link_libraries(testtio2 testdep)
target_link_libraries(testvfsresolve testdep)
