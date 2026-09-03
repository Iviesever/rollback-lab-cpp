cmake_minimum_required(VERSION 3.28)

project(rollback_lab VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RollbackLabWarnings)

option(ROLLBACK_LAB_BUILD_TESTS "Build rollback-lab tests" ON)
option(ROLLBACK_LAB_BUILD_BENCHMARKS "Build rollback-lab benchmarks" ON)

add_library(rollback_lab STATIC
    src/version.cpp
    src/core/hash.cpp
    src/netcode/session.cpp
    src/simulation/scripted_input.cpp
    src/simulation/simulation.cpp)
add_library(rollback_lab::rollback_lab ALIAS rollback_lab)
target_compile_features(rollback_lab PUBLIC cxx_std_23)
target_include_directories(rollback_lab PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
rollback_lab_set_warnings(rollback_lab)

if(ROLLBACK_LAB_BUILD_TESTS AND BUILD_TESTING)
    add_executable(rollback_lab_tests
        tests/test_main.cpp
        tests/test_smoke.cpp
        tests/unit/core_simulation_test.cpp
        tests/unit/rollback_session_test.cpp)
    target_link_libraries(rollback_lab_tests PRIVATE rollback_lab)
    target_include_directories(rollback_lab_tests PRIVATE tests)
    rollback_lab_set_warnings(rollback_lab_tests)
    add_test(NAME rollback_lab_tests COMMAND rollback_lab_tests)
endif()
