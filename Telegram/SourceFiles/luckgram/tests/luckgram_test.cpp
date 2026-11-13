/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/tests/luckgram_test.h"

#include <cstdio>
#include <exception>

namespace Luckgram::Test {
namespace {

struct Failure {
	QString text;
};

} // namespace

std::vector<Case> &Registry() {
	static auto result = std::vector<Case>();
	return result;
}

void Fail(const char *file, int line, const QString &message) {
	throw Failure{ QStringLiteral("%1:%2: %3")
		.arg(QString::fromUtf8(file))
		.arg(line)
		.arg(message) };
}

} // namespace Luckgram::Test

LUCKGRAM_TEST(FrameworkSelfTest) {
	LG_CHECK(true);
	LG_EQUAL(1 + 1, 2);
	LG_EQUAL(QStringLiteral("a") + QStringLiteral("b"), QStringLiteral("ab"));
	auto failed = false;
	try {
		LG_EQUAL(1, 2);
	} catch (...) {
		failed = true;
	}
	LG_CHECK(failed);
}

int main(int argc, char *argv[]) {
	const auto filter = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
	auto passed = 0;
	auto failed = 0;
	for (const auto &test : Luckgram::Test::Registry()) {
		const auto name = QString::fromUtf8(test.name);
		if (!filter.isEmpty() && !name.contains(filter)) {
			continue;
		}
		try {
			test.run();
			++passed;
		} catch (const Luckgram::Test::Failure &failure) {
			++failed;
			std::fprintf(
				stderr,
				"FAIL %s\n  %s\n",
				test.name,
				failure.text.toUtf8().constData());
		} catch (const std::exception &e) {
			++failed;
			std::fprintf(stderr, "FAIL %s\n  exception: %s\n", test.name, e.what());
		} catch (...) {
			++failed;
			std::fprintf(stderr, "FAIL %s\n  unknown exception\n", test.name);
		}
	}
	std::printf("luckgram_tests: %d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
