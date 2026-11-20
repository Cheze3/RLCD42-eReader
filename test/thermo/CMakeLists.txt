add_executable(ThermoTest ThermoTest.cpp ../../lib/Thermo/src/thermo/Thermo.cpp)

target_include_directories(ThermoTest PRIVATE ../../lib/Thermo/src)

target_link_libraries(ThermoTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ThermoTest)
