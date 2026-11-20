add_executable(NoteQuizTest NoteQuizTest.cpp ../../lib/NoteQuiz/src/notequiz/NoteQuiz.cpp)

target_include_directories(NoteQuizTest PRIVATE ../../lib/NoteQuiz/src)

target_link_libraries(NoteQuizTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NoteQuizTest)
