cmake_minimum_required(VERSION 3.16)
project(DateFormatting LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(GENERATED_I18N "${CMAKE_CURRENT_BINARY_DIR}/i18n")
file(MAKE_DIRECTORY ${GENERATED_I18N})
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/gen_i18n.py
          ${REPO_ROOT}/lib/I18n/translations ${GENERATED_I18N}
  WORKING_DIRECTORY ${REPO_ROOT}
  RESULT_VARIABLE I18N_RESULT
  OUTPUT_QUIET
)
if(NOT I18N_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate date translation test data")
endif()
# Copy I18n sources so quoted includes use these generated keys, independently of PlatformIO builds.
configure_file(${REPO_ROOT}/lib/I18n/I18n.h ${GENERATED_I18N}/I18n.h COPYONLY)
configure_file(${REPO_ROOT}/lib/I18n/I18n.cpp ${GENERATED_I18N}/I18n.cpp COPYONLY)
configure_file(${REPO_ROOT}/lib/I18n/DateFormatting.cpp ${GENERATED_I18N}/DateFormatting.cpp COPYONLY)
add_executable(date_formatting DateFormattingTest.cpp
  ${GENERATED_I18N}/I18n.cpp ${GENERATED_I18N}/I18nStrings.cpp ${GENERATED_I18N}/DateFormatting.cpp)
target_include_directories(date_formatting PRIVATE ${REPO_ROOT}/include ${GENERATED_I18N})
target_compile_features(date_formatting PRIVATE cxx_std_17)
target_compile_options(date_formatting PRIVATE -UNDEBUG)
add_test(NAME date_formatting COMMAND date_formatting)
