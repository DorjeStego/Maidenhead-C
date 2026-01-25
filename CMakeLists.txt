cmake_minimum_required(VERSION 3.20)
project(maidenhead LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the CPython extension module for CLion builds.
find_package(Python COMPONENTS Interpreter Development.Module Development REQUIRED)

set(MAIDENHEAD_NATIVE_SOURCES
    src/maidenhead/_native/maidenhead_native.c
    src/maidenhead/_native/core.c
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

add_executable(
    mh_cli
    src/maidenhead/_native/cli.c
    src/maidenhead/_native/cli_main.c
    src/maidenhead/_native/core.c
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

find_package(GeographicLib QUIET)
if(GeographicLib_FOUND)
    target_sources(_native PRIVATE src/maidenhead/_native/geo_geodesic.cpp)
    target_compile_definitions(_native PRIVATE MH_HAVE_GEODESIC=1)
    target_sources(mh_cli PRIVATE src/maidenhead/_native/geo_geodesic.cpp)
    target_compile_definitions(mh_cli PRIVATE MH_HAVE_GEODESIC=1)
    if(TARGET GeographicLib::GeographicLib)
        target_link_libraries(_native PRIVATE GeographicLib::GeographicLib)
        target_link_libraries(mh_cli PRIVATE GeographicLib::GeographicLib)
    else()
        target_link_libraries(_native PRIVATE GeographicLib)
        target_link_libraries(mh_cli PRIVATE GeographicLib)
    endif()
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
