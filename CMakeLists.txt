cmake_minimum_required(VERSION 3.10)
project(socket_io_serverpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(UUID REQUIRED uuid)

# Find Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # If GTest not found, download and build it
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50f33f9142fd2d6f0fa8a30b17ca3.zip
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Enable testing
enable_testing()

# Add include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson/internal
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi
    ${PROJECT_SOURCE_DIR}/external/websocketpp
)

# Collect source files
file(GLOB_RECURSE SOURCES
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/*.cpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common/*.cpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/*.cpp
)

# Add main examples (usage.cpp, test1.cpp, test_refactoring.cpp)
add_executable(usage examples/usage.cpp ${SOURCES})
add_executable(test1 examples/test1.cpp ${SOURCES})

# Link against UUID library
target_link_libraries(usage ${UUID_LIBRARIES})
target_link_libraries(test1 ${UUID_LIBRARIES})

# Optionally, you can add targets for static/shared library
# add_library(socket_io_serverpp STATIC ${SOURCES})
# add_library(socket_io_serverpp SHARED ${SOURCES})

# Add test directory
add_subdirectory(tests)
