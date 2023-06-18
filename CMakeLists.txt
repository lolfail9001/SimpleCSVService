cmake_minimum_required (VERSION 3.14.0)

project(simplecsvservice)

set (CMAKE_EXPORT_COMPILE_COMMANDS true)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

set (CMAKE_CXX_FLAGS "-g")

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/001f281926c74a819e9266bb4ec525515ff39418.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(SQLite3)

include_directories(${SQLITE3_INCLUDE_DIRECTORIES})
include_directories()

add_executable (
  csvapp
  csvloadapp.hpp
  csvloadapp.cpp
  cpp-httplib/httplib.h
  csvapp.cpp
)

target_link_libraries(csvapp ${SQLite3_LIBRARIES})

enable_testing()

add_executable (
  csvtests
  csvtests.cpp
  csvloadapp.hpp
  csvloadapp.cpp
  cpp-httplib/httplib.h)
target_link_libraries (csvtests ${SQLite3_LIBRARIES} gtest_main)

include(GoogleTest)
gtest_discover_tests(csvtests)
