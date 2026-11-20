add_executable(MusicPlayerTest
  MusicPlayerTest.cpp
  ../../lib/MusicPlayer/src/music/Id3.cpp
  ../../lib/MusicPlayer/src/music/Mp3Stream.cpp
  ../../lib/MusicPlayer/src/music/PlayQueue.cpp
)

target_include_directories(MusicPlayerTest PRIVATE ../../lib/MusicPlayer/src)
target_compile_definitions(MusicPlayerTest PRIVATE MUSIC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# minimp3 is third-party code: keep its warnings out of our -Wall -Wextra -pedantic build.
set_source_files_properties(../../lib/MusicPlayer/src/music/Mp3Stream.cpp PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(MusicPlayerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MusicPlayerTest)
