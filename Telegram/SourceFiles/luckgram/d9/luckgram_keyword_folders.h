/*
This file is part of Luckgram Desktop, based on Telegram Desktop.

Luckgram changes are licensed under GPL-3.0 like the upstream project.
*/
#pragma once

#include "luckgram/d9/luckgram_keyword_logic.h"
#include "settings/settings_common_session.h"
#include "ui/wrap/slide_wrap.h"

class History;

namespace Data {
class ChatFilter;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
class FlatLabel;
class GenericBox;
class LinkButton;
class PlainShadow;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// D9 / mobile 06: local keyword folders. Each one is shown as a chat folder
// (a Data::ChatFilter with an id from kFilterIdBase up) next to the cloud
// folders and can be reordered with them, but is never sent to Telegram:
// Data::ChatFilters skips local ids whenever it talks to the server. Stored
// per account, encrypted (luckgram/luckgram_storage.h). Matching rules live in
// luckgram_keyword_logic.h.
namespace Luckgram::Keywords {

inline constexpr auto kFeatureKey = "keyword_folders";
// Cloud folder ids are 2..255; local keyword folders use ids from here up.
inline constexpr auto kFilterIdBase = FilterId(1'000'000);

[[nodiscard]] bool Enabled();
[[nodiscard]] bool IsLocalFilterId(FilterId id);

// ---- Data::ChatFilters / Data::Session hooks.

// Data::ChatFilter::contains() for a local filter id.
[[nodiscard]] bool Contains(not_null<History*> history, FilterId id);
// Replaces the local entries of `list` with the current keyword folders, at
// their remembered positions (none while the switch is off).
void ArrangeLocal(
	not_null<Main::Session*> session,
	std::vector<Data::ChatFilter> &list);
// The folder order was changed (tab bar drag): remember local positions.
void RememberOrder(
	not_null<Main::Session*> session,
	const std::vector<FilterId> &order);
// Keywords to highlight in chat list previews of this folder, or empty.
[[nodiscard]] const QStringList &HighlightKeywords(
	not_null<Main::Session*> session,
	FilterId id);

// ---- Folders.

[[nodiscard]] std::vector<Folder> Folders(not_null<Main::Session*> session);
[[nodiscard]] std::optional<Folder> FindByFilterId(
	not_null<Main::Session*> session,
	FilterId id);
[[nodiscard]] bool CanCreate(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<> FoldersChanges(
	not_null<Main::Session*> session);

// ---- UI.

// 「新建关键词文件夹」 / 「编辑关键词文件夹」.
void ShowCreate(not_null<Window::SessionController*> controller);
void ShowEdit(
	not_null<Window::SessionController*> controller,
	FilterId id);
void ConfirmDelete(
	not_null<Window::SessionController*> controller,
	FilterId id);

[[nodiscard]] QString Title(); // 关键词文件夹
[[nodiscard]] QString MenuNewFolder(); // 新建关键词文件夹

// 「跳到下一个『发票』」 in a chat opened from a keyword folder (mobile 06
// §4). Desktop shows it as a bar under the chat's top bars instead of above
// the input field (the input field area belongs to D6 / D7). Same interface
// as HistoryView::TranslateBar, for HistoryWidget.
class JumpBar final {
public:
	JumpBar(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~JumpBar();

	void raise();
	void finishAnimating();
	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	struct Hit {
		MsgId id = 0;
		QString keyword;
	};

	void setup();
	void relayout();
	void updateShadowGeometry(QRect wrapGeometry);
	void refreshShown();
	void refreshLabel();
	void jump();
	[[nodiscard]] std::vector<Hit> collectHits() const;
	[[nodiscard]] QStringList keywords() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	Ui::SlideWrap<Ui::AbstractButton> _wrap;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	Ui::FlatLabel *_label = nullptr;
	Ui::LinkButton *_close = nullptr;
	FilterId _filterId = 0;
	MsgId _lastJumped = 0;
	int _width = 0;
	bool _closed = false;

};

} // namespace Luckgram::Keywords

namespace Settings {

// 「关键词文件夹」: the folders with their keywords, 「新建关键词文件夹」 and
// the "this device only" note. Opened from Luckgram 功能 and from
// Settings → Folders.
class LuckgramKeywordFolders : public Section<LuckgramKeywordFolders> {
public:
	LuckgramKeywordFolders(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
