ida_add_plugin(bromaida
    TYPE QT
    QT_COMPONENTS Core Gui Widgets
    SOURCES
        exporter/exporter.cpp
        env/ida_env.cpp
        env/ida_state.cpp
        importer/ida_types.cpp
        ui/ida_ui.cpp
        importer/importer.cpp
        plugin.cpp
        ui/qt_ui.cpp
        ui/settings_ui.cpp
    DEFINES
        # IDA's Qt is built inside a namespace; the plugin has to speak it
        QT_NAMESPACE=QT
        QT_NO_KEYWORDS
    LIBRARIES bromaida::core
)
target_include_directories(bromaida PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# IDA already has Qt loaded: never let a build-tree RPATH pull in a second copy
set_target_properties(bromaida PROPERTIES SKIP_BUILD_RPATH ON)

if(WIN32)
    # the namespaced symbols live in the import libraries Hex-Rays ships with the SDK
    target_link_libraries(bromaida PRIVATE
        ${IDA_LIB_DIR}/../${IDA_ARCH}_win_qt/Qt6Core.lib
        ${IDA_LIB_DIR}/../${IDA_ARCH}_win_qt/Qt6Gui.lib
        ${IDA_LIB_DIR}/../${IDA_ARCH}_win_qt/Qt6Widgets.lib)
elseif(APPLE)
    # resolved against IDA's own Qt frameworks when the plugin is loaded
    target_link_options(bromaida PRIVATE -undefined dynamic_lookup)
endif()

# The plugin looks for its data (profiles, SDK bundles, STL flavors) in a "bromaida" folder next to itself.
add_custom_command(TARGET bromaida POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/data $<TARGET_FILE_DIR:bromaida>/bromaida
    COMMENT "Copying profiles, SDK bundles and STL flavors next to the plugin"
)
install(TARGETS bromaida DESTINATION plugins)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/ DESTINATION plugins/bromaida)
