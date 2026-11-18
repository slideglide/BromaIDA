# bromaida-layoutcheck needs libclang (the stable C API), like bromaida-sdkgen.
find_path(BROMAIDA_CLANG_INCLUDE_DIR clang-c/Index.h PATH_SUFFIXES llvm clang)
find_library(BROMAIDA_CLANG_LIBRARY NAMES clang libclang)
if(NOT BROMAIDA_CLANG_INCLUDE_DIR OR NOT BROMAIDA_CLANG_LIBRARY)
    message(FATAL_ERROR "bromaida-layoutcheck needs libclang (clang-c/Index.h and libclang).")
endif()

add_executable(bromaida_layoutcheck main.cpp)
set_target_properties(bromaida_layoutcheck PROPERTIES OUTPUT_NAME bromaida-layoutcheck)
target_include_directories(bromaida_layoutcheck SYSTEM PRIVATE ${BROMAIDA_CLANG_INCLUDE_DIR})
target_link_libraries(bromaida_layoutcheck PRIVATE bromaida::core ${BROMAIDA_CLANG_LIBRARY})
target_compile_definitions(bromaida_layoutcheck PRIVATE BROMAIDA_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
bromaida_enable_warnings(bromaida_layoutcheck)

# The stand-ins must survive being instantiated, which needs no game files.
if(BROMAIDA_BUILD_TESTS)
    foreach(_case msvc:win libcxx-apple:imac libcxx-apple:m1 libcxx-ndk:android64 libcxx-ndk:android32
                  gnustl:android64 gnustl:android32)
        string(REPLACE ":" ";" _parts ${_case})
        list(GET _parts 0 _stl)
        list(GET _parts 1 _platform)
        add_test(NAME stl_instantiate_${_stl}_${_platform}
            COMMAND bromaida_layoutcheck --instantiate ${PROJECT_SOURCE_DIR}/tests/data/stl_instantiate.cpp
                    --stl ${_stl} --platform ${_platform})
    endforeach()
endif()

# The known sizes come from real binaries, and so do the bindings: the tests only run when BROMAIDA_TEST_BINDINGS names
# a checkout of geode-sdk/bindings (its `bindings` folder, which holds one folder per game version).
if(BROMAIDA_BUILD_TESTS)
    set(_sizes ${PROJECT_SOURCE_DIR}/tests/data/sizes)
    foreach(_case
            2.2091:android64 2.2091:android32 2.2091:win 2.2091:imac 2.2091:m1 2.2091:ios
            2.2081:android64 2.2081:android32)
        string(REPLACE ":" ";" _parts ${_case})
        list(GET _parts 0 _version)
        list(GET _parts 1 _platform)
        add_test(NAME layout_${_version}_${_platform}
            COMMAND bromaida_layoutcheck --version ${_version} --platform ${_platform}
                    --sizes ${_sizes}/${_version}-${_platform}.txt)
        set_tests_properties(layout_${_version}_${_platform} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
