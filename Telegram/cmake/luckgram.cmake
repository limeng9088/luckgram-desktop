# Luckgram Desktop additions (D1). Kept in one file so upstream merges only
# touch the single include() line in Telegram/CMakeLists.txt.

set(LUCKGRAM_BUILD_NUMBER "0" CACHE STRING "CI run number shown on the about page.")
set(LUCKGRAM_COMMIT "" CACHE STRING "Git commit shown on the about page.")
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
)

target_compile_definitions(Telegram
PRIVATE
    LUCKGRAM_BUILD_NUMBER=${LUCKGRAM_BUILD_NUMBER}
    LUCKGRAM_COMMIT="${LUCKGRAM_COMMIT}"
)
