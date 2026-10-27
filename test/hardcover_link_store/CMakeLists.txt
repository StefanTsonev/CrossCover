add_executable(HardcoverBookLinkStoreTest
  HardcoverBookLinkStoreTest.cpp
  ${REPO_ROOT}/lib/Hardcover/HardcoverBookLinkStore.cpp
)
target_include_directories(HardcoverBookLinkStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Hardcover
)
target_link_libraries(HardcoverBookLinkStoreTest PRIVATE crosspoint_test_common ArduinoJson)
add_test(NAME HardcoverBookLinkStoreTest COMMAND HardcoverBookLinkStoreTest)
