add_executable(testtws1 testtws1.c)
target_link_libraries(testtws1 tinypile)

add_executable(tws_stress tws_stress.cpp)
target_link_libraries(tws_stress tinypile)
