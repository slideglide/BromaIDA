cmake_minimum_required(VERSION 3.25)

project(BromaIDA
    VERSION 9.0.0
    DESCRIPTION "Broma support for IDA"
    HOMEPAGE_URL "https://github.com/Stazzical/BromaIDA"
    LANGUAGES CXX
)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the core ends up inside a shared library

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(_bida_default_tests ON)
else()
    set(_bida_default_tests OFF)
endif()

option(BROMAIDA_BUILD_PLUGIN "Build the IDA plugin (needs the IDA SDK)" ON)
option(BROMAIDA_BUILD_TESTS "Build the unit tests" ${_bida_default_tests})
option(BROMAIDA_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
set(BROMAIDA_IDASDK_DIR "" CACHE PATH
    "Path to an IDA SDK checkout (the folder holding src/). Defaults to $IDASDK, then to a pinned download.")
set(BROMAIDA_QT_VERSION "6.8" CACHE STRING "Qt major.minor of the IDA release (IDA 9.4 ships 6.8)")
set(BROMAIDA_QT_DIR "" CACHE PATH "Qt kit to build against, e.g. ~/Qt/6.8.2/gcc_64. Defaults to $QT_ROOT_DIR, then ~/Qt.")
option(BROMAIDA_ALLOW_NEWER_QT "Allow a Qt newer than IDA's; the plugin may then fail to load" OFF)
set(BROMAIDA_IDASDK_TAG "v9.4.0-sdk.1" CACHE STRING "IDA SDK tag to download when no SDK is given")

include(BromaIDADependencies)
include(BromaIDAWarnings)

add_subdirectory(src/core)

if(BROMAIDA_BUILD_PLUGIN)
    add_subdirectory(src/ida)
endif()

if(BROMAIDA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
