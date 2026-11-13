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

    # D2 encrypted storage naming
    luckgram/luckgram_storage_logic.cpp
    luckgram/luckgram_storage_logic.h
    luckgram/tests/luckgram_storage_tests.cpp

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
    luckgram/media/luckgram_image_prepare.cpp
    luckgram/media/luckgram_image_prepare.h
    luckgram/tests/luckgram_discover_tests.cpp
    luckgram/tests/luckgram_media_tests.cpp
    luckgram/media/luckgram_round_video_logic.cpp
    luckgram/media/luckgram_round_video_logic.h
    luckgram/tests/luckgram_round_video_tests.cpp
    luckgram/calls/luckgram_call_recording_logic.cpp
    luckgram/calls/luckgram_call_recording_logic.h
    luckgram/tests/luckgram_call_recording_tests.cpp
    luckgram/tests/luckgram_nearby_tests.cpp
    luckgram/tests/luckgram_square_tests.cpp

    # D12 Luckgram Pro, account link
    luckgram/pro/luckgram_pro_logic.cpp
    luckgram/pro/luckgram_pro_logic.h
    luckgram/tests/luckgram_pro_logic_tests.cpp

    # D3 proxy import and exit check (mobile specs#117 / #118); the
    # import logic calls D12's luckgram/xray/luckgram_xray_logic.
    luckgram/proxy/luckgram_proxy_exit_logic.cpp
    luckgram/proxy/luckgram_proxy_exit_logic.h
    luckgram/proxy/luckgram_proxy_import_logic.cpp
    luckgram/proxy/luckgram_proxy_import_logic.h
    luckgram/tests/luckgram_proxy_tests.cpp

    # D8 double space, hidden chats, store-restricted channels (11, 12, 13)
    luckgram/luckgram_privacy_logic.cpp
    luckgram/luckgram_privacy_logic.h
    luckgram/tests/luckgram_privacy_tests.cpp

    # D12 built-in Xray: share links, Clash entries, outbounds, config
    luckgram/xray/luckgram_xray_logic.cpp
    luckgram/xray/luckgram_xray_logic.h
    luckgram/tests/luckgram_xray_logic_tests.cpp

    # D6 messages and language (mobile 01, 02, 14, 15, 17)
    luckgram/translate/translate_logic.cpp
    luckgram/translate/translate_logic.h
    luckgram/translate/translate_logic_tests.cpp
    luckgram/quickreplies/quick_replies_logic.cpp
    luckgram/quickreplies/quick_replies_logic.h
    luckgram/tests/luckgram_quick_replies_tests.cpp
    luckgram/selectall/select_all_logic.cpp
    luckgram/selectall/select_all_logic.h
    luckgram/tests/luckgram_select_all_tests.cpp
    luckgram/transcribe/luckgram_transcribe_logic.cpp
    luckgram/transcribe/luckgram_transcribe_logic.h
    luckgram/tests/luckgram_transcribe_tests.cpp

    # D9 profile details, private notes, keyword folders, shared-group
    # activity, storage options (mobile 05 / 06 / 07 / 08 / 16)
    luckgram/d9/luckgram_cache_logic.cpp
    luckgram/d9/luckgram_cache_logic.h
    luckgram/d9/luckgram_keyword_logic.cpp
    luckgram/d9/luckgram_keyword_logic.h
    luckgram/d9/luckgram_notes_text.cpp
    luckgram/d9/luckgram_notes_text.h
    luckgram/d9/luckgram_profile_info_logic.cpp
    luckgram/d9/luckgram_profile_info_logic.h
    luckgram/d9/luckgram_shared_activity_logic.cpp
    luckgram/d9/luckgram_shared_activity_logic.h
    luckgram/tests/luckgram_d9_tests.cpp

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
    luckgram/ai/ai_text_logic.cpp
    luckgram/ai/ai_text_logic.h
    luckgram/ai/ai_compose_logic_tests.cpp
    luckgram/ai/ai_conversation_logic_tests.cpp
    luckgram/ai/ai_market_logic_tests.cpp
    luckgram/ai/ai_markdown_logic_tests.cpp
    luckgram/ai/ai_summary_logic_tests.cpp

    # D12 account backup file (PBKDF2 / AES-GCM: OpenSSL, linked below)
    luckgram/backup/luckgram_backup_format.cpp
    luckgram/backup/luckgram_backup_format.h
    luckgram/tests/luckgram_backup_format_tests.cpp
    luckgram/ai/ai_text_logic_tests.cpp
)

target_link_libraries(luckgram_tests
PRIVATE
    desktop-app::external_qt
    desktop-app::external_qt_static_plugins # JPEG / PNG (D10 media tests).

    desktop-app::external_openssl # D12 backup format.
)

set_target_properties(luckgram_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    EXCLUDE_FROM_ALL ON
)
