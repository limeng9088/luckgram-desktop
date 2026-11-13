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

    # D5 welcome, privacy, onboarding, version, login code help
    luckgram/luckgram_onboarding_logic.cpp
    luckgram/luckgram_onboarding_logic.h
    luckgram/tests/luckgram_onboarding_tests.cpp

    # D10 discover, square, people nearby, scan, block list
    luckgram/discover/luckgram_discover_logic.cpp
    luckgram/discover/luckgram_discover_logic.h
    luckgram/discover/luckgram_nearby_logic.cpp
    luckgram/discover/luckgram_nearby_logic.h
    luckgram/discover/luckgram_scan_logic.cpp
    luckgram/discover/luckgram_scan_logic.h
    luckgram/discover/luckgram_square_logic.cpp
    luckgram/discover/luckgram_square_logic.h
    luckgram/tests/luckgram_discover_tests.cpp
    luckgram/tests/luckgram_nearby_tests.cpp
    luckgram/tests/luckgram_square_tests.cpp

    # D12 Luckgram Pro, account link
    luckgram/pro/luckgram_pro_logic.cpp
    luckgram/pro/luckgram_pro_logic.h
    luckgram/tests/luckgram_pro_logic_tests.cpp

    # D8 double space, hidden chats, store-restricted channels (11, 12, 13)
    luckgram/luckgram_privacy_logic.cpp
    luckgram/luckgram_privacy_logic.h
    luckgram/tests/luckgram_privacy_tests.cpp

    # D7 AI (mobile 18-24, 30)
    luckgram/ai/ai_agents_logic.cpp
    luckgram/ai/ai_agents_logic.h
    luckgram/ai/ai_compose_logic.cpp
    luckgram/ai/ai_compose_logic.h
    luckgram/ai/ai_conversation_logic.cpp
    luckgram/ai/ai_conversation_logic.h
    luckgram/ai/ai_market_logic.cpp
    luckgram/ai/ai_market_logic.h
    luckgram/ai/ai_markdown_logic.cpp
    luckgram/ai/ai_markdown_logic.h
    luckgram/ai/ai_summary_logic.cpp
    luckgram/ai/ai_summary_logic.h
    luckgram/ai/ai_compose_logic_tests.cpp
    luckgram/ai/ai_conversation_logic_tests.cpp
    luckgram/ai/ai_market_logic_tests.cpp
    luckgram/ai/ai_markdown_logic_tests.cpp
    luckgram/ai/ai_summary_logic_tests.cpp
)

target_link_libraries(luckgram_tests
PRIVATE
    desktop-app::external_qt
)

set_target_properties(luckgram_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    EXCLUDE_FROM_ALL ON
)
