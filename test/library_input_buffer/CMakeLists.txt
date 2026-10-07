add_executable(LibraryInputBufferTest LibraryInputBufferTest.cpp)
target_include_directories(LibraryInputBufferTest PRIVATE
  ${REPO_ROOT}/src/activities/library
  ${REPO_ROOT}/lib/LibraryIndex
)
target_link_libraries(LibraryInputBufferTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryInputBufferTest)
