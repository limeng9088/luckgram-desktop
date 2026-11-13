# Luckgram unit tests: one executable for the pure logic of all desktop
# tasks (D5-D12). Configure with -D LUCKGRAM_TESTS=ON, build the target
# luckgram_tests, run it from the build folder (exit code 0 = all passed;
# an argument filters tests by name).
#
# Add a task's pure logic .cpp (QtCore + std only, compiled into Telegram as
# well) and its *_tests.cpp below. Tests register themselves with
# LUCKGRAM_TEST(name), see luckgram/tests/luckgram_test.h.

add_executable(luckgram_tests)
init_target(luckgram_tests "(luckgram)")

target_include_directories(luckgram_tests PRIVATE ${src_loc})

nice_target_sources(luckgram_tests ${src_loc}
PRIVATE
    luckgram/tests/luckgram_test.cpp
    luckgram/tests/luckgram_test.h

    # D5-D12: logic files and their tests, one block per task.
)

target_link_libraries(luckgram_tests
PRIVATE
    desktop-app::external_qt
)

set_target_properties(luckgram_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    EXCLUDE_FROM_ALL ON
)
