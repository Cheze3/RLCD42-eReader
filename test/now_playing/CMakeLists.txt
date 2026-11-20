add_executable(NowPlayingTest NowPlayingTest.cpp ../../lib/NowPlaying/src/nowplaying/Protocol.cpp)

target_include_directories(NowPlayingTest PRIVATE ../../lib/NowPlaying/src)
target_compile_definitions(NowPlayingTest PRIVATE NOWPLAYING_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_link_libraries(NowPlayingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NowPlayingTest)
