cmake_minimum_required(VERSION 2.8)

get_filename_component(SAMPLE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

project(${SAMPLE_PROJECT})

find_package(Threads)

# define some folders
get_filename_component(RXCPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PATH)
get_filename_component(RXCPP_DIR "${RXCPP_DIR}" PATH)
get_filename_component(RXCPP_DIR "${RXCPP_DIR}" PATH)
get_filename_component(RXCPP_DIR "${RXCPP_DIR}" PATH)

message(STATUS "RXCPP_DIR: " ${RXCPP_DIR} )

# define the sources
set(SAMPLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

add_executable(
  ${SAMPLE_PROJECT}
  WIN32
  ${SAMPLE_SOURCES}
  )

target_compile_options(
  ${SAMPLE_PROJECT}
  PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-std=c++11 -stdlib=libc++ -Wall -Wextra -Werror -ftemplate-depth=1024>
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Werror -std=c++11>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4503 /wd4702 /wd4091 /bigobj>
  )

target_include_directories(
  ${SAMPLE_PROJECT}
  SYSTEM
  PRIVATE
  ${RXCPP_DIR}/ext/catch/include
  )

target_include_directories(
  ${SAMPLE_PROJECT}
  PRIVATE
  ${RXCPP_DIR}/Ix/CPP/src ${RXCPP_DIR}/Rx/v2/src
  )

target_compile_definitions(
  ${SAMPLE_PROJECT}
  PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/DUNICODE /D_UNICODE>
  )

target_link_libraries(
  ${SAMPLE_PROJECT}
  PRIVATE
  ${CMAKE_THREAD_LIBS_INIT}
  )

# configure unit tests via CTest
enable_testing()
set(CTEST_CONFIGURATION_TYPE "${JOB_BUILD_CONFIGURATION}")

add_test(NAME RunTests
     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
     COMMAND ${SAMPLE_PROJECT} ${TEST_ARGS}
     )
