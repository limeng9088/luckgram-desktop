/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include "luckgram/luckgram_storage_logic.h"

// D2 encrypted storage: naming rules that keep Luckgram files apart from
// Telegram's own ones and make logout cleanup find all of them.

namespace {

using namespace Luckgram::Storage;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(text);
}

} // namespace

LUCKGRAM_TEST(StorageValidNames) {
	LG_CHECK(IsValidName(U("translate_cache")));
	LG_CHECK(IsValidName(U("ai-chat-2")));
	LG_CHECK(IsValidName(QString(kMaxNameLength, QChar('a'))));
}

LUCKGRAM_TEST(StorageRejectsBadNames) {
	LG_CHECK(!IsValidName(QString()));
	LG_CHECK(!IsValidName(QString(kMaxNameLength + 1, QChar('a'))));
	LG_CHECK(!IsValidName(U("../map")));
	LG_CHECK(!IsValidName(U("a/b")));
	LG_CHECK(!IsValidName(U("a\\b")));
	LG_CHECK(!IsValidName(U("Notes")));
	LG_CHECK(!IsValidName(U("notes.json")));
	LG_CHECK(!IsValidName(U("备注")));
	LG_CHECK(!IsValidName(U("a b")));
}

LUCKGRAM_TEST(StorageFileNameHasPrefix) {
	LG_EQUAL(FileName(U("notes")), U("luckgram_notes"));
	LG_EQUAL(FileName(U("../notes")), QString());
}

LUCKGRAM_TEST(StorageTelegramFilesAreNotOurs) {
	LG_CHECK(IsLuckgramFile(U("luckgram_notess")));
	LG_CHECK(!IsLuckgramFile(U("luckgram_")));
	LG_CHECK(!IsLuckgramFile(U("D877F783D5D3EF8Cs")));
	LG_CHECK(!IsLuckgramFile(U("maps")));
	LG_CHECK(!IsLuckgramFile(U("settingss")));
	LG_CHECK(!IsLuckgramFile(U("luckgram.json")));
	LG_CHECK(!IsLuckgramFile(U("tgw")));
}

LUCKGRAM_TEST(StorageRecordPathsCoverEverySuffix) {
	const auto paths = RecordPaths(U("/data/A/"), U("notes"));
	LG_EQUAL(paths.size(), 3);
	LG_CHECK(paths.contains(U("/data/A/luckgram_notes0")));
	LG_CHECK(paths.contains(U("/data/A/luckgram_notes1")));
	LG_CHECK(paths.contains(U("/data/A/luckgram_notess")));
	LG_CHECK(RecordPaths(U("/data/A/"), U("x/y")).isEmpty());
}

LUCKGRAM_TEST(StorageCleanupKeepsTelegramFiles) {
	const auto names = QStringList{
		U("maps"),
		U("D877F783D5D3EF8Cs"),
		U("luckgram_notess"),
		U("luckgram_translate_cache0"),
		U("luckgram_translate_cache1"),
		U("configs"),
	};
	const auto paths = LuckgramPaths(U("/data/A/"), names);
	LG_EQUAL(paths.size(), 3);
	LG_CHECK(paths.contains(U("/data/A/luckgram_notess")));
	LG_CHECK(paths.contains(U("/data/A/luckgram_translate_cache0")));
	LG_CHECK(paths.contains(U("/data/A/luckgram_translate_cache1")));
}
