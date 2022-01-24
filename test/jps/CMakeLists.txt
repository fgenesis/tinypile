add_library(scenarioloader ScenarioLoader.cpp ScenarioLoader.h)

add_executable(testjps1 testjps1.cpp ../../jps.hh)
add_executable(testjps2 testjps2.cpp ../../jps.hh)
add_executable(testjps3 testjps3.cpp ../../jps.hh)
add_executable(testjps4 testjps4.cpp ../../jps.hh)

target_link_libraries(testjps2 scenarioloader)
