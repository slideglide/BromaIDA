configure_file(
    ${PROJECT_SOURCE_DIR}/include/bromaida/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bromaida/version.hpp
    @ONLY
)

add_library(bromaida_core STATIC
    platform/binary_info.cpp
    broma/bindings.cpp
    typegen/bundle.cpp
    broma/broma_io.cpp
    typegen/class_graph.cpp
    typegen/codegen.cpp
    util/digest.cpp
    exporter/exporter.cpp
    util/files.cpp
    util/log.cpp
    util/path.cpp
    typegen/profile.cpp
    config/settings.cpp
    broma/signature.cpp
    typegen/stl_types.cpp
    exporter/symbols.cpp
    util/text.cpp
)
add_library(bromaida::core ALIAS bromaida_core)

target_include_directories(bromaida_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(bromaida_core PUBLIC cxx_std_23)
target_link_libraries(bromaida_core PUBLIC Broma GeodeResult fmt::fmt glaze::glaze asp)
bromaida_enable_warnings(bromaida_core)
