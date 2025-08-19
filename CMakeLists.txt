cmake_minimum_required(VERSION 3.10)
project(socket_io_serverpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(UUID REQUIRED uuid)

# Find Boost with required components
find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please install libboost-dev")
endif()

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
    ${Boost_INCLUDE_DIRS}
)

# Collect source files (excluding SCGI)
file(GLOB_RECURSE SOURCES
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/*.cpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common/*.cpp
)

# Exclude SCGI files if they exist
file(GLOB_RECURSE SCGI_SOURCES ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/*.cpp)
list(REMOVE_ITEM SOURCES ${SCGI_SOURCES})

# Add main examples (usage.cpp, test1.cpp, usage_polling.cpp)
add_executable(usage examples/usage.cpp ${SOURCES})
add_executable(test1 examples/test1.cpp ${SOURCES})
add_executable(usage_polling examples/usage_polling.cpp ${SOURCES})

# Link against required libraries
target_link_libraries(usage ${UUID_LIBRARIES})
target_link_libraries(test1 ${UUID_LIBRARIES})
target_link_libraries(usage_polling ${UUID_LIBRARIES})

# Link Boost libraries
target_link_libraries(usage ${Boost_LIBRARIES})
target_link_libraries(test1 ${Boost_LIBRARIES})
target_link_libraries(usage_polling ${Boost_LIBRARIES})

# Optionally, you can add targets for static/shared library
# add_library(socket_io_serverpp STATIC ${SOURCES})
# add_library(socket_io_serverpp SHARED ${SOURCES})

# Add test directory
# add_subdirectory(tests)
