add_executable(bromaida_tests
    main.cpp
    test_text.cpp
    test_symbols.cpp
    test_broma_io.cpp
    test_exporter.cpp
    test_codegen.cpp
    test_profile.cpp
    test_real_bindings.cpp
)
target_include_directories(bromaida_tests PRIVATE ${doctest_SOURCE_DIR})
target_link_libraries(bromaida_tests PRIVATE bromaida::core)
target_compile_definitions(bromaida_tests PRIVATE BROMAIDA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME bromaida_tests COMMAND bromaida_tests)
