# Luckgram D8: pure logic of double space / hidden chats / store-restricted
# channels (luckgram_privacy_logic), same cases as the mobile unit tests.

add_executable(test_luckgram_privacy)
init_non_host_target(test_luckgram_privacy "(tests)")

target_precompile_headers(test_luckgram_privacy PRIVATE ${src_loc}/tests/test_luckgram_privacy_pch.h)
nice_target_sources(test_luckgram_privacy ${src_loc}
PRIVATE
    tests/test_luckgram_privacy.cpp
    tests/test_luckgram_privacy_pch.h
    luckgram/luckgram_privacy_logic.cpp
    luckgram/luckgram_privacy_logic.h
)

target_include_directories(test_luckgram_privacy
PRIVATE
    ${src_loc}
)

target_link_libraries(test_luckgram_privacy
PRIVATE
    desktop-app::lib_base
)

set_target_properties(test_luckgram_privacy PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_luckgram_privacy
)
