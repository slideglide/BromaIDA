# bromaida-sdkgen needs libclang (the stable C API): any LLVM 14+ works.
find_path(BROMAIDA_CLANG_INCLUDE_DIR clang-c/Index.h PATH_SUFFIXES llvm clang)
find_library(BROMAIDA_CLANG_LIBRARY NAMES clang libclang)
if(NOT BROMAIDA_CLANG_INCLUDE_DIR OR NOT BROMAIDA_CLANG_LIBRARY)
    message(FATAL_ERROR "bromaida-sdkgen needs libclang (clang-c/Index.h and libclang). "
                        "Install LLVM's development package or build with -DBROMAIDA_BUILD_SDKGEN=OFF.")
endif()

add_executable(bromaida_sdkgen main.cpp)
set_target_properties(bromaida_sdkgen PROPERTIES OUTPUT_NAME bromaida-sdkgen)
target_include_directories(bromaida_sdkgen SYSTEM PRIVATE ${BROMAIDA_CLANG_INCLUDE_DIR})
target_link_libraries(bromaida_sdkgen PRIVATE bromaida::core ${BROMAIDA_CLANG_LIBRARY})
target_compile_definitions(bromaida_sdkgen PRIVATE
    BROMAIDA_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    BROMAIDA_SDKGEN_STUBS="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
bromaida_enable_warnings(bromaida_sdkgen)
