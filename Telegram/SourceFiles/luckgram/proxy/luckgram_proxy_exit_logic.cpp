/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#include "luckgram/proxy/luckgram_proxy_exit_logic.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>

#include <map>

namespace Luckgram::ProxyExit {
namespace {

[[nodiscard]] bool IsCode(const QString &value) {
	return value.size() == 2
		&& value[0] >= QChar('A') && value[0] <= QChar('Z')
		&& value[1] >= QChar('A') && value[1] <= QChar('Z');
}

// Chinese names of the usual exit countries; Qt has no Chinese display
// names for most territories, the rest falls back to English.
[[nodiscard]] const std::map<QString, const char*> &ChineseNames() {
	static const auto result = std::map<QString, const char*>{
		{ QStringLiteral("AE"), "阿联酋" },
		{ QStringLiteral("AR"), "阿根廷" },
		{ QStringLiteral("AT"), "奥地利" },
		{ QStringLiteral("AU"), "澳大利亚" },
		{ QStringLiteral("BE"), "比利时" },
		{ QStringLiteral("BR"), "巴西" },
		{ QStringLiteral("CA"), "加拿大" },
		{ QStringLiteral("CH"), "瑞士" },
		{ QStringLiteral("CL"), "智利" },
		{ QStringLiteral("CN"), "中国" },
		{ QStringLiteral("CO"), "哥伦比亚" },
		{ QStringLiteral("CZ"), "捷克" },
		{ QStringLiteral("DE"), "德国" },
		{ QStringLiteral("DK"), "丹麦" },
		{ QStringLiteral("EG"), "埃及" },
		{ QStringLiteral("ES"), "西班牙" },
		{ QStringLiteral("FI"), "芬兰" },
		{ QStringLiteral("FR"), "法国" },
		{ QStringLiteral("GB"), "英国" },
		{ QStringLiteral("GR"), "希腊" },
		{ QStringLiteral("HK"), "中国香港" },
		{ QStringLiteral("HU"), "匈牙利" },
		{ QStringLiteral("ID"), "印度尼西亚" },
		{ QStringLiteral("IE"), "爱尔兰" },
		{ QStringLiteral("IL"), "以色列" },
		{ QStringLiteral("IN"), "印度" },
		{ QStringLiteral("IT"), "意大利" },
		{ QStringLiteral("JP"), "日本" },
		{ QStringLiteral("KH"), "柬埔寨" },
		{ QStringLiteral("KR"), "韩国" },
		{ QStringLiteral("KZ"), "哈萨克斯坦" },
		{ QStringLiteral("LA"), "老挝" },
		{ QStringLiteral("MM"), "缅甸" },
		{ QStringLiteral("MO"), "中国澳门" },
		{ QStringLiteral("MX"), "墨西哥" },
		{ QStringLiteral("MY"), "马来西亚" },
		{ QStringLiteral("NG"), "尼日利亚" },
		{ QStringLiteral("NL"), "荷兰" },
		{ QStringLiteral("NO"), "挪威" },
		{ QStringLiteral("NZ"), "新西兰" },
		{ QStringLiteral("PE"), "秘鲁" },
		{ QStringLiteral("PH"), "菲律宾" },
		{ QStringLiteral("PK"), "巴基斯坦" },
		{ QStringLiteral("PL"), "波兰" },
		{ QStringLiteral("PT"), "葡萄牙" },
		{ QStringLiteral("RO"), "罗马尼亚" },
		{ QStringLiteral("RU"), "俄罗斯" },
		{ QStringLiteral("SA"), "沙特阿拉伯" },
		{ QStringLiteral("SE"), "瑞典" },
		{ QStringLiteral("SG"), "新加坡" },
		{ QStringLiteral("TH"), "泰国" },
		{ QStringLiteral("TR"), "土耳其" },
		{ QStringLiteral("TW"), "中国台湾" },
		{ QStringLiteral("UA"), "乌克兰" },
		{ QStringLiteral("US"), "美国" },
		{ QStringLiteral("VE"), "委内瑞拉" },
		{ QStringLiteral("VN"), "越南" },
		{ QStringLiteral("ZA"), "南非" },
	};
	return result;
}

[[nodiscard]] QString EnglishName(const QString &country) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
	const auto territory = QLocale::codeToTerritory(country);
	return (territory == QLocale::AnyTerritory)
		? QString()
		: QLocale::territoryToString(territory);
#else // Qt >= 6.2
	const auto territory = QLocale::codeToCountry(country);
	return (territory == QLocale::AnyCountry)
		? QString()
		: QLocale::countryToString(territory);
#endif // Qt >= 6.2
}

} // namespace

Info MakeInfo(const QString &ip, const QString &country, const QString &city) {
	const auto code = country.trimmed().toUpper();
	const auto known = IsCode(code)
		&& code != QStringLiteral("XX")
		&& code != QStringLiteral("T1");
	return Info{
		.ip = ip.trimmed(),
		.country = known ? code : QString(),
		.city = city.trimmed(),
	};
}

std::optional<Info> Parse(const QByteArray &json) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto info = MakeInfo(
		object.value(QStringLiteral("ip")).toString(),
		object.value(QStringLiteral("country")).toString(),
		object.value(QStringLiteral("city")).toString());
	if (info.ip.isEmpty()) {
		return std::nullopt;
	}
	return info;
}

QString Flag(const QString &country) {
	if (!IsCode(country)) {
		return QString();
	}
	auto result = QString();
	for (const auto c : country) {
		const auto code = char32_t(0x1F1E6 + (c.unicode() - 'A'));
		result.append(QString::fromUcs4(&code, 1));
	}
	return result;
}

QString CountryName(const QString &country, bool zh) {
	if (country.isEmpty()) {
		return QString();
	}
	if (zh) {
		const auto &names = ChineseNames();
		const auto i = names.find(country);
		if (i != names.end()) {
			return QString::fromUtf8(i->second);
		}
	}
	const auto english = EnglishName(country);
	return english.isEmpty() ? country : english;
}

QString Label(const Info &info, bool zh) {
	if (info.country.isEmpty()) {
		return info.ip;
	}
	return Flag(info.country)
		+ QChar(' ')
		+ CountryName(info.country, zh)
		+ QStringLiteral(" · ")
		+ info.ip;
}

bool IsFresh(qint64 checkedAtMs, qint64 nowMs) {
	return checkedAtMs > 0
		&& nowMs - checkedAtMs >= 0
		&& nowMs - checkedAtMs < kTtlMs;
}

} // namespace Luckgram::ProxyExit
