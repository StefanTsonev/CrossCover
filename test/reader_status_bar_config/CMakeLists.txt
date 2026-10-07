FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
)
FetchContent_MakeAvailable(arduinojson)

add_executable(ReaderStatusBarConfigTest ReaderStatusBarConfigTest.cpp)
target_include_directories(ReaderStatusBarConfigTest PRIVATE ${REPO_ROOT}/src/util ${arduinojson_SOURCE_DIR}/src)
target_link_libraries(ReaderStatusBarConfigTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderStatusBarConfigTest)
