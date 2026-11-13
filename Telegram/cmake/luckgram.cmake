# Luckgram Desktop additions (D1). Kept in one file so upstream merges only
# touch the single include() line in Telegram/CMakeLists.txt.

set(LUCKGRAM_BUILD_NUMBER "0" CACHE STRING "CI run number shown on the about page.")
set(LUCKGRAM_COMMIT "" CACHE STRING "Git commit shown on the about page.")
option(LUCKGRAM_CI_BUILD "CI screenshot build only: screenshot mode and Telegram test-server login. Never for releases." OFF)
option(LUCKGRAM_DEV "Dev build: api-dev.luckgram.cc, debug switches and page (D2)." OFF)
option(LUCKGRAM_TESTS "Build luckgram_tests (Luckgram pure logic unit tests)." OFF)
option(LUCKGRAM_REQUIRE_API_CREDENTIALS "Fail unless real api_id / api_hash are given (CI)." OFF)

if (LUCKGRAM_REQUIRE_API_CREDENTIALS)
    if (TDESKTOP_API_TEST)
        message(FATAL_ERROR "Luckgram: TDESKTOP_API_TEST is not allowed, provide TG_API_ID / TG_API_HASH.")
    endif()
    if (NOT TDESKTOP_API_ID MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Luckgram: TDESKTOP_API_ID (secret TG_API_ID) is missing or not a number.")
    endif()
    if (NOT TDESKTOP_API_HASH MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Luckgram: TDESKTOP_API_HASH (secret TG_API_HASH) is missing or not hex.")
    endif()
endif()

nice_target_sources(Telegram ${src_loc}
PRIVATE
    luckgram/luckgram_brand.cpp
    luckgram/luckgram_brand.h
    luckgram/luckgram_desktop.cpp
    luckgram/luckgram_desktop.h
    luckgram/luckgram_intro.cpp
    luckgram/luckgram_intro.h
    luckgram/discover/luckgram_discover_logic.cpp
    luckgram/discover/luckgram_discover_logic.h
    luckgram/discover/luckgram_nearby_logic.cpp
    luckgram/discover/luckgram_nearby_logic.h
    luckgram/discover/luckgram_scan_logic.cpp
    luckgram/discover/luckgram_scan_logic.h
    luckgram/discover/luckgram_square_logic.cpp
    luckgram/discover/luckgram_square_logic.h
    luckgram/discover/luckgram_square_compose.cpp
    luckgram/discover/luckgram_square_compose.h
    luckgram/discover/luckgram_silent_link.cpp
    luckgram/discover/luckgram_silent_link.h
    luckgram/discover/luckgram_square_ui.cpp
    luckgram/discover/luckgram_square_ui.h
    luckgram/media/luckgram_image_prepare.cpp
    luckgram/media/luckgram_image_prepare.h
    luckgram/media/luckgram_video_transcode.cpp
    luckgram/media/luckgram_video_transcode.h
    settings/sections/settings_luckgram_discover.cpp
    settings/sections/settings_luckgram_discover.h
    luckgram/luckgram_onboarding.cpp
    luckgram/luckgram_onboarding.h
    luckgram/luckgram_onboarding_logic.cpp
    luckgram/luckgram_onboarding_logic.h
    luckgram/luckgram_screenshots.cpp
    luckgram/luckgram_screenshots.h
    luckgram/luckgram_ci_login.cpp
    luckgram/luckgram_ci_login.h
    # D2 foundation
    luckgram/luckgram_ai.cpp
    luckgram/luckgram_ai.h
    luckgram/luckgram_api.cpp
    luckgram/luckgram_api.h
    luckgram/luckgram_compose_slots.cpp
    luckgram/luckgram_compose_slots.h
    luckgram/luckgram_features.cpp
    luckgram/luckgram_features.h
    luckgram/luckgram_save_to_saved.cpp
    luckgram/luckgram_save_to_saved.h
    luckgram/luckgram_secure_store.cpp
    luckgram/luckgram_secure_store.h
    luckgram/luckgram_service.cpp
    luckgram/luckgram_service.h
    luckgram/luckgram_settings.cpp
    luckgram/luckgram_settings.h
    luckgram/luckgram_storage.cpp
    luckgram/luckgram_storage.h
    luckgram/luckgram_storage_logic.cpp
    luckgram/luckgram_storage_logic.h
    luckgram/luckgram_strings.cpp
    luckgram/luckgram_strings.h
    luckgram/translate/translate_logic.cpp
    luckgram/translate/translate_logic.h
    luckgram/translate/translate_service.cpp
    luckgram/translate/translate_service.h
    settings/sections/settings_luckgram.cpp
    settings/sections/settings_luckgram.h
    # D12 Pro, account link
    luckgram/pro/luckgram_pro.cpp
    luckgram/pro/luckgram_pro.h
    luckgram/pro/luckgram_pro_logic.cpp
    luckgram/pro/luckgram_pro_logic.h
    settings/sections/settings_luckgram_pro.cpp
    settings/sections/settings_luckgram_pro.h
    # D12 built-in Xray
    luckgram/xray/luckgram_xray.cpp
    luckgram/xray/luckgram_xray.h
    luckgram/xray/luckgram_xray_logic.cpp
    luckgram/xray/luckgram_xray_logic.h
    settings/sections/settings_luckgram_translate.cpp
    settings/sections/settings_luckgram_translate.h
    # D7 AI: pure logic (also in luckgram_tests)
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
)

# A generated header rather than a compile definition: switching the option
# rebuilds only luckgram_ci_login.cpp / luckgram_screenshots.cpp and keeps the precompiled header valid.
if (LUCKGRAM_CI_BUILD)
    set(LUCKGRAM_CI_BUILD_VALUE 1)
else()
    set(LUCKGRAM_CI_BUILD_VALUE 0)
endif()
file(CONFIGURE
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/luckgram_gen/luckgram_ci_config.h
    CONTENT "#pragma once\n#define LUCKGRAM_CI_BUILD @LUCKGRAM_CI_BUILD_VALUE@\n"
    @ONLY)
target_include_directories(Telegram PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/luckgram_gen)

if (LUCKGRAM_DEV)
    target_compile_definitions(Telegram PRIVATE LUCKGRAM_DEV_BUILD)
endif()

target_compile_definitions(Telegram
PRIVATE
    LUCKGRAM_BUILD_NUMBER=${LUCKGRAM_BUILD_NUMBER}
    LUCKGRAM_COMMIT="${LUCKGRAM_COMMIT}"
)

# D8 account & privacy: double space (11), hidden chats (12), store-restricted
# channels (13). Own block so other feature branches don't touch these lines.
nice_target_sources(Telegram ${src_loc}
PRIVATE
    luckgram/luckgram_double_bottom.cpp
    luckgram/luckgram_double_bottom.h
    luckgram/luckgram_hidden_chats.cpp
    luckgram/luckgram_hidden_chats.h
    luckgram/luckgram_privacy_logic.cpp
    luckgram/luckgram_privacy_logic.h
    luckgram/luckgram_privacy_ui.cpp
    luckgram/luckgram_privacy_ui.h
    luckgram/luckgram_store_restrictions.cpp
    luckgram/luckgram_store_restrictions.h
    settings/sections/settings_luckgram_privacy.cpp
    settings/sections/settings_luckgram_privacy.h
)

if (LUCKGRAM_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/luckgram_tests.cmake)
endif()

# D3 proxy: paste link, QR from a picture, subscriptions, exit country / IP
# (mobile specs#116-#118).
nice_target_sources(Telegram ${src_loc}
PRIVATE
    luckgram/proxy/luckgram_proxy_exit.cpp
    luckgram/proxy/luckgram_proxy_exit.h
    luckgram/proxy/luckgram_proxy_exit_logic.cpp
    luckgram/proxy/luckgram_proxy_exit_logic.h
    luckgram/proxy/luckgram_proxy_import.cpp
    luckgram/proxy/luckgram_proxy_import.h
    luckgram/proxy/luckgram_proxy_import_logic.cpp
    luckgram/proxy/luckgram_proxy_import_logic.h
    luckgram/qr/luckgram_qr_decode.cpp
    luckgram/qr/luckgram_qr_decode.h
)

# QR decoding (D3 pictures, D10 camera): vendored quirc 1.2, ISC license,
# https://github.com/dlbeer/quirc tag v1.2, lib/ files unchanged. C code in
# its own library so it stays out of the C++ precompiled header.
add_library(lib_luckgram_quirc STATIC)
init_target(lib_luckgram_quirc "(external)")
nice_target_sources(lib_luckgram_quirc ${src_loc}
PRIVATE
    luckgram/qr/quirc/decode.c
    luckgram/qr/quirc/identify.c
    luckgram/qr/quirc/quirc.c
    luckgram/qr/quirc/quirc.h
    luckgram/qr/quirc/quirc_internal.h
    luckgram/qr/quirc/version_db.c
)
target_include_directories(lib_luckgram_quirc PUBLIC ${src_loc}/luckgram/qr/quirc)
if (MSVC)
    target_compile_options(lib_luckgram_quirc PRIVATE /W0 /WX-)
else()
    target_compile_options(lib_luckgram_quirc PRIVATE -w)
endif()
if (LINUX)
    target_link_libraries(lib_luckgram_quirc PUBLIC m)
endif()
target_link_libraries(Telegram PRIVATE lib_luckgram_quirc)
