set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.12.0)
project(ParallelLauncher VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project can only be built on Linux!")
endif()

include(FetchContent)
include(CTest)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/contrib)

include(Catch)

find_package(spdlog REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ParallelLauncher
src/SignalHandler.cpp
src/ThreadManager.cpp
src/main.cpp
)

add_executable(ParallelLauncher_tests
tests/test_ThreadManager.cpp
)

target_link_libraries(ParallelLauncher PRIVATE pthread spdlog::spdlog)
target_link_libraries(ParallelLauncher_tests PRIVATE pthread spdlog::spdlog Catch2::Catch2WithMain)
target_include_directories(ParallelLauncher_tests PRIVATE ${CMAKE_SOURCE_DIR}/includes)
target_include_directories(ParallelLauncher PRIVATE ${CMAKE_SOURCE_DIR}/includes)

enable_testing()
add_test(NAME ParallelLauncher_unit COMMAND ParallelLauncher_tests)

catch_discover_tests(ParallelLauncher_tests)