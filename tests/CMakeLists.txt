# Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

# Include directories for tests
include_directories(
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/common
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/lib/rapidjson/internal
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi
    ${PROJECT_SOURCE_DIR}/external/websocketpp
)

# Create a library for testable code (header-only, so we need the sources)
set(TESTABLE_SOURCES
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/Service.h
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/Request.h
    ${PROJECT_SOURCE_DIR}/socket.io-serverpp/scgi/Netstring.h
)

# Test source files
set(TEST_SOURCES
    test_constants.cpp
    test_error.cpp
    test_logger.cpp
    test_message.cpp
    test_event.cpp
    test_socket.cpp
    test_socketnamespace.cpp
    test_sioserver.cpp
    test_integration.cpp
    main.cpp
)

# Create test executable
# Add test executable
add_executable(run_tests
    test_constants.cpp
    test_error.cpp
    test_logger.cpp
    test_message.cpp
    test_event.cpp
    test_socket.cpp
    test_socketnamespace.cpp
    test_sioserver.cpp
    test_integration.cpp
)

# Link libraries
target_link_libraries(run_tests 
    GTest::gtest_main
    ${UUID_LIBRARIES}
    pthread
)

# Add test discovery
include(GoogleTest)
gtest_discover_tests(run_tests)
