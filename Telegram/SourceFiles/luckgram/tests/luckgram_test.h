/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

// Luckgram unit tests (all desktop tasks share this one target).
//
// Build: configure with -D LUCKGRAM_TESTS=ON, then build the target
// luckgram_tests and run it (exit code 0 = all passed). See
// Telegram/cmake/luckgram_tests.cmake.
//
// What can be tested here: pure logic files that need only QtCore and the
// standard library. They must not include the app's precompiled header
// stuff (no u"..."_q, no rpl, no lang keys, no styles), so the same .cpp
// compiles into both Telegram and luckgram_tests. Keep UI in other files.
//
// Writing a test (any file listed in luckgram_tests.cmake):
//
//   #include "luckgram/tests/luckgram_test.h"
//   #include "luckgram/luckgram_something_logic.h"
//
//   LUCKGRAM_TEST(SomethingParsesEmptyInput) {
//       LG_CHECK(Luckgram::Something::Parse(QString()).isEmpty());
//       LG_EQUAL(Luckgram::Something::Count(), 3);
//   }

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <string>
#include <type_traits>
#include <vector>

namespace Luckgram::Test {

struct Case {
	const char *name = nullptr;
	void (*run)() = nullptr;
};

[[nodiscard]] std::vector<Case> &Registry();

struct Registrar {
	Registrar(const char *name, void (*run)()) {
		Registry().push_back({ name, run });
	}
};

// Throws: the runner reports the failure and goes on with the next test.
[[noreturn]] void Fail(const char *file, int line, const QString &message);

inline QString ToString(const QString &value) {
	return QChar('"') + value + QChar('"');
}
inline QString ToString(const char *value) {
	return ToString(QString::fromUtf8(value));
}
inline QString ToString(const std::string &value) {
	return ToString(QString::fromStdString(value));
}
inline QString ToString(const QStringList &value) {
	auto result = QStringList();
	for (const auto &item : value) {
		result.push_back(ToString(item));
	}
	return QChar('[') + result.join(QStringLiteral(", ")) + QChar(']');
}
inline QString ToString(bool value) {
	return value ? QStringLiteral("true") : QStringLiteral("false");
}
template <typename T>
QString ToString(const T &value) {
	if constexpr (std::is_enum_v<T>) {
		return QString::number(static_cast<long long>(value));
	} else if constexpr (std::is_floating_point_v<T>) {
		return QString::number(value, 'g', 10);
	} else if constexpr (std::is_integral_v<T>) {
		return QString::number(static_cast<long long>(value));
	} else {
		return QStringLiteral("(value)");
	}
}

} // namespace Luckgram::Test

#define LUCKGRAM_TEST(name) \
	static void name(); \
	static const ::Luckgram::Test::Registrar name##Registrar(#name, &name); \
	static void name()

#define LG_CHECK(condition) \
	do { \
		if (!(condition)) { \
			::Luckgram::Test::Fail( \
				__FILE__, \
				__LINE__, \
				QStringLiteral(#condition)); \
		} \
	} while (false)

#define LG_EQUAL(actual, expected) \
	do { \
		const auto &lgActual = (actual); \
		const auto &lgExpected = (expected); \
		if (!(lgActual == lgExpected)) { \
			::Luckgram::Test::Fail( \
				__FILE__, \
				__LINE__, \
				QStringLiteral(#actual " == " #expected ": got ") \
					+ ::Luckgram::Test::ToString(lgActual) \
					+ QStringLiteral(", expected ") \
					+ ::Luckgram::Test::ToString(lgExpected)); \
		} \
	} while (false)
