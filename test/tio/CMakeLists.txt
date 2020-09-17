add_executable(testtio1 testtio1.cpp)
add_executable(testtio2 testtio2.cpp)

target_link_libraries(testtio1 testdep tinypile)
target_link_libraries(testtio2 testdep tinypile)