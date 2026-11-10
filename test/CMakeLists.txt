cmake_minimum_required(VERSION 3.16)
project(host_tests CXX)

include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.7.0
)
FetchContent_MakeAvailable(unity)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

# Hardware-free firmware sources, compiled for the host.
add_executable(test_logic
    test_logic/test_main.cpp
    test_logic/test_protocol.cpp
    test_logic/test_queue.cpp
    test_logic/test_session.cpp
    test_logic/test_model.cpp
    ../main/miot_protocol.cpp
    ../main/miot_queue.cpp
    ../main/miot_session.cpp
    ../main/purifier_model.cpp
)
target_include_directories(test_logic PRIVATE ../main)
target_link_libraries(test_logic PRIVATE unity)
target_compile_options(test_logic PRIVATE -std=gnu++17 -Wall -Wextra -Werror)
