# Luckgram Desktop D9 (profile & organization: mobile 05 / 06 / 07 / 08 / 16).
# Its own file so the D-tasks running in parallel don't all edit
# luckgram.cmake.

nice_target_sources(Telegram ${src_loc}
PRIVATE
    luckgram/d9/luckgram_account_store.cpp
    luckgram/d9/luckgram_account_store.h
    luckgram/d9/luckgram_cache_logic.cpp
    luckgram/d9/luckgram_cache_logic.h
    luckgram/d9/luckgram_keyword_folders.cpp
    luckgram/d9/luckgram_keyword_folders.h
    luckgram/d9/luckgram_keyword_logic.cpp
    luckgram/d9/luckgram_keyword_logic.h
    luckgram/d9/luckgram_notes.cpp
    luckgram/d9/luckgram_notes.h
    luckgram/d9/luckgram_notes_text.cpp
    luckgram/d9/luckgram_notes_text.h
    luckgram/d9/luckgram_notes_widgets.cpp
    luckgram/d9/luckgram_notes_widgets.h
    luckgram/d9/luckgram_profile_info.cpp
    luckgram/d9/luckgram_profile_info.h
    luckgram/d9/luckgram_profile_info_logic.cpp
    luckgram/d9/luckgram_profile_info_logic.h
    luckgram/d9/luckgram_shared_activity.cpp
    luckgram/d9/luckgram_shared_activity.h
    luckgram/d9/luckgram_shared_activity_logic.cpp
    luckgram/d9/luckgram_shared_activity_logic.h
)

# Unit tests of the pure D9 rules (no UI, no session): build and run
#   cmake --build . --target test_luckgram_d9
#   ./test_luckgram_d9/test_luckgram_d9
add_executable(test_luckgram_d9)
init_non_host_target(test_luckgram_d9 "(tests)")

nice_target_sources(test_luckgram_d9 ${src_loc}
PRIVATE
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
    luckgram/d9/test_luckgram_d9.cpp
)

target_include_directories(test_luckgram_d9
PRIVATE
    ${src_loc}
)

target_link_libraries(test_luckgram_d9
PRIVATE
    desktop-app::lib_base
)

set_target_properties(test_luckgram_d9 PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_luckgram_d9
)
