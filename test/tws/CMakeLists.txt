add_executable(testtws0 testtws0.c)
target_link_libraries(testtws0 testdep tws)

#add_executable(testtws1 testtws1.c)
#target_link_libraries(testtws1 testdep)

add_executable(testtws_split testtws_split.c)
target_link_libraries(testtws_split testdep tws)

#add_executable(tws_stress tws_stress.cpp)
#target_link_libraries(tws_stress testdep)

#add_executable(testtwscpp testtwscpp.cpp)
#target_link_libraries(testtwscpp testdep)

#add_executable(testtwscppasync testtwscppasync.cpp)
#target_link_libraries(testtwscppasync testdep)
