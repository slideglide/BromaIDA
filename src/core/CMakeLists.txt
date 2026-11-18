configure_file(
    ${PROJECT_SOURCE_DIR}/include/bromaida/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bromaida/version.hpp
    @ONLY
)

add_library(bromaida_core STATIC
    binary_info.cpp
    bundle.cpp
    broma_io.cpp
    class_graph.cpp
    codegen.cpp
    exporter.cpp
    log.cpp
    profile.cpp
    settings.cpp
    signature.cpp
    stl_types.cpp
    symbols.cpp
    text.cpp
)
add_library(bromaida::core ALIAS bromaida_core)

target_include_directories(bromaida_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(bromaida_core PUBLIC cxx_std_23)
target_link_libraries(bromaida_core PUBLIC Broma GeodeResult fmt::fmt glaze::glaze asp)
bromaida_enable_warnings(bromaida_core)
