cmake_minimum_required(VERSION 3.20)
project(maidenhead LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WITH_SLEEF_SIMD "Enable SIMD math backend via SLEEF" OFF)
option(EMIT_ASM "Emit assembly alongside objects (-save-temps=obj)" OFF)

# Build the CPython extension module for CLion builds.
find_package(Python COMPONENTS Interpreter Development.Module Development REQUIRED)

set(MAIDENHEAD_NATIVE_SOURCES
    src/maidenhead/_native/maidenhead_native.c
    src/maidenhead/_native/core.c
    src/maidenhead/_native/core_utils.c
    src/maidenhead/_native/geo.c
    src/maidenhead/_native/coverage.c
    src/maidenhead/_native/geojson.c
    src/maidenhead/_native/json_simdjson.cpp
)

add_library(_native MODULE ${MAIDENHEAD_NATIVE_SOURCES})

target_include_directories(
    _native
    PRIVATE
        ${Python_INCLUDE_DIRS}
        ${CMAKE_CURRENT_LIST_DIR}/src/maidenhead/_native
)

target_link_libraries(_native PRIVATE Python::Module)

# simdjson is required for _native json_loads.
find_path(SIMDJSON_INCLUDE_DIR simdjson.h)
find_library(SIMDJSON_LIBRARY simdjson)
if(NOT SIMDJSON_INCLUDE_DIR OR NOT SIMDJSON_LIBRARY)
    message(FATAL_ERROR "simdjson not found but required for native build")
endif()
target_include_directories(_native PRIVATE ${SIMDJSON_INCLUDE_DIR})
target_link_libraries(_native PRIVATE ${SIMDJSON_LIBRARY})
target_compile_definitions(_native PRIVATE MH_HAVE_SIMDJSON=1)

add_executable(
    mh_cli
    src/maidenhead/_native/cli.c
    src/maidenhead/_native/cli_bulk.c
    src/maidenhead/_native/cli_core_ops.c
    src/maidenhead/_native/cli_cover_ops.c
    src/maidenhead/_native/cli_geo_ops.c
    src/maidenhead/_native/cli_main.c
    src/maidenhead/_native/cli_commands.c
    src/maidenhead/_native/cli_spatial_ops.c
    src/maidenhead/_native/cli_utils.c
    src/maidenhead/_native/core.c
    src/maidenhead/_native/core_utils.c
    src/maidenhead/_native/geo.c
    src/maidenhead/_native/coverage.c
    src/maidenhead/_native/geojson.c
)

target_include_directories(
    mh_cli
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/maidenhead/_native
)

target_link_libraries(mh_cli PRIVATE)
if(UNIX AND NOT APPLE)
    target_link_libraries(mh_cli PRIVATE m)
endif()

if(EMIT_ASM)
    foreach(_tgt IN ITEMS _native mh_cli)
        target_compile_options(
            ${_tgt}
            PRIVATE
                $<$<C_COMPILER_ID:GNU,Clang>:-save-temps=obj>
                $<$<CXX_COMPILER_ID:GNU,Clang>:-save-temps=obj>
        )
    endforeach()
endif()

if(WITH_SLEEF_SIMD)
    set(SLEEF_HINT $ENV{SLEEF_DIR})
    find_path(
        SLEEF_INCLUDE_DIR sleef.h
        HINTS ${SLEEF_HINT}
        PATH_SUFFIXES include
    )
    find_library(
        SLEEF_LIBRARY sleef
        HINTS ${SLEEF_HINT}
        PATH_SUFFIXES lib lib64
    )
    if(NOT SLEEF_INCLUDE_DIR OR NOT SLEEF_LIBRARY)
        message(FATAL_ERROR "WITH_SLEEF_SIMD=ON but sleef not found")
    endif()
    target_include_directories(_native PRIVATE ${SLEEF_INCLUDE_DIR})
    target_include_directories(mh_cli PRIVATE ${SLEEF_INCLUDE_DIR})
    target_link_libraries(_native PRIVATE ${SLEEF_LIBRARY})
    target_link_libraries(mh_cli PRIVATE ${SLEEF_LIBRARY})
    target_compile_definitions(_native PRIVATE MH_HAVE_SIMD_MATH=1)
    target_compile_definitions(mh_cli PRIVATE MH_HAVE_SIMD_MATH=1)
endif()

# Ensure the extension has the correct suffix and no "lib" prefix.
set_target_properties(_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "_native"
    SUFFIX "${Python_EXTENSION_SUFFIX}"
)

# Install into the Python package directory (optional).
install(TARGETS _native DESTINATION maidenhead)
install(TARGETS mh_cli DESTINATION bin)

# CLion-friendly custom targets
add_custom_target(
    native_tests
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target _native
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_LIST_DIR}/scripts/run_tests_native.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Run native tests (cached build)"
)

add_custom_target(
    native_tests_rebuild
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target _native
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_LIST_DIR}/scripts/run_tests_native.sh --rebuild
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Run native tests (force rebuild)"
)

add_custom_target(
    native_tests_debug
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target _native
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_LIST_DIR}/scripts/debug_tests_native.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Debug native tests (gdb)"
)

add_custom_target(
    native_clean_build
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target _native
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Clean and build native extension"
)
