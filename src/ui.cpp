#include "ui.h"

#include "matcher.h"
#include "functions.h"
#include "tmux.h"
#include "types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace {

const Color kMatchFg = Color::RGB(10, 25, 45);
const Color kMatchBg = Color::RGB(121, 192, 255);
const Color kActiveIndicator = Color::RGB(92, 196, 132);
const Color kUserColor = Color::RGB(201, 145, 60);
const Color kHostColor = Color::RGB(52, 150, 198);
const Color kSearchPromptColor = Color::RGB(72, 118, 198);
const Color kSearchCursorBg = Color::RGB(210, 221, 238);
const Color kSearchCursorFg = Color::RGB(25, 31, 40);
const Color kSelectedRowBg = Color::RGB(62, 69, 82);
const Color kSelectedRowFg = Color::RGB(236, 241, 248);
const Color kCopiedRowBg = Color::RGB(52, 140, 86);
constexpr int kAliasIdentityGap = 10;
constexpr int kMaxVisibleRows = 10;

std::vector<HostMatch> FilterHostMatches(const std::vector<Host> &hosts,
										 const std::string &query) {
	return RankHosts(hosts, query);
}

int ComputeAliasColumnWidth(const std::vector<Host> &hosts) {
	int max_alias_column_width = 1;
	for (const Host &host : hosts) {
		const int alias_width =
			static_cast<int>(host.alias.empty() ? 1 : host.alias.size());
		const int indicator_width = host.isActive ? 1 : 0;
		max_alias_column_width =
			std::max(max_alias_column_width, alias_width + indicator_width);
	}
	return max_alias_column_width;
}

int ComputePickerWidth(const std::vector<Host> &hosts, int alias_column_width) {
	constexpr int kMinPickerWidth = 24;
	constexpr int kMaxPickerWidth = 110;

	int max_row_content_width = 0;
	for (const Host &host : hosts) {
		const int user_width =
			static_cast<int>(host.user.empty() ? 1 : host.user.size());
		const int host_width =
			static_cast<int>(host.hostname.empty() ? 1 : host.hostname.size());

		const int row_content_width = 1 + alias_column_width +
									  kAliasIdentityGap + user_width + 1 +
									  host_width + 1;
		max_row_content_width =
			std::max(max_row_content_width, row_content_width);
	}

	const int picker_width = max_row_content_width + 2;
	return std::clamp(picker_width, kMinPickerWidth, kMaxPickerWidth);
}

Element RenderAliasWithMatches(const std::string &alias,
							   const std::vector<int> &positions) {
	if (alias.empty()) {
		return text("-");
	}

	std::set<int> matched(positions.begin(), positions.end());
	Elements letters;
	letters.reserve(alias.size());

	for (size_t i = 0; i < alias.size(); ++i) {
		Element ch = text(std::string(1, alias[i]));
		if (matched.count(static_cast<int>(i)) > 0) {
			ch = ch | bold | color(kMatchFg) | bgcolor(kMatchBg);
		}
		letters.push_back(ch);
	}

	return hbox(std::move(letters));
}

Element RenderSearchQuery(const std::string &query, int cursor_position,
						  int width) {
	if (width <= 0) {
		return text("");
	}

	if (query.empty()) {
		const int trailing_space_count = std::max(0, width - 1);

		Elements parts = {
			text(" ") | color(kSearchCursorFg) | bgcolor(kSearchCursorBg),
		};
		if (trailing_space_count > 0) {
			parts.push_back(text(std::string(trailing_space_count, ' ')));
		}
		return hbox(std::move(parts));
	}

	const int safe_cursor =
		std::max(0, std::min(cursor_position, static_cast<int>(query.size())));
	const int window_start = std::max(
		0, std::min(safe_cursor - width + 1, static_cast<int>(query.size())));
	const int visible_len = std::min(
		std::max(0, width - 1), static_cast<int>(query.size()) - window_start);
	const std::string visible =
		query.substr(window_start, static_cast<size_t>(visible_len));
	const int cursor_in_visible = safe_cursor - window_start;

	Elements parts;
	const int safe_cursor_in_visible =
		std::max(0, std::min(cursor_in_visible, visible_len));
	const std::string left =
		visible.substr(0, static_cast<size_t>(safe_cursor_in_visible));
	const std::string right =
		visible.substr(static_cast<size_t>(safe_cursor_in_visible));
	const int trailing_space_count = width - visible_len - 1;

	parts.push_back(text(left));
	parts.push_back(text(" ") | color(kSearchCursorFg) | bgcolor(kSearchCursorBg));
	parts.push_back(text(right));
	if (trailing_space_count > 0) {
		parts.push_back(text(std::string(trailing_space_count, ' ')));
	}

	return hbox(std::move(parts));
}

void ClampSelection(int &selected, int max_count) {
	if (max_count <= 0) {
		selected = 0;
		return;
	}

	selected = std::max(0, std::min(selected, max_count - 1));
}

void AdjustScrollOffset(int &scroll_offset, int selected, int max_count,
						int window_size) {
	if (max_count <= 0 || window_size <= 0 || max_count <= window_size) {
		scroll_offset = 0;
		return;
	}

	const int max_offset = max_count - window_size;
	scroll_offset = std::max(0, std::min(scroll_offset, max_offset));

	if (selected < scroll_offset) {
		scroll_offset = selected;
	}
	if (selected >= scroll_offset + window_size) {
		scroll_offset = selected - window_size + 1;
	}
}

} // namespace

int RunHostPickerUI(std::vector<Host> &hosts) {
	std::string query;
	int input_cursor_position = 0;
	int selected = 0;
	int scroll_offset = 0;
	std::vector<HostMatch> visible_matches = FilterHostMatches(hosts, query);
	const int alias_column_width = ComputeAliasColumnWidth(hosts);
	const int picker_width = ComputePickerWidth(hosts, alias_column_width);
	const int max_picker_height = kMaxVisibleRows + 3;
	auto copy_feedback_until = std::chrono::steady_clock::time_point::min();

	InputOption input_option;
	input_option.placeholder = "";
	input_option.cursor_position = &input_cursor_position;
	input_option.multiline = false;
	Component input = Input(&query, input_option);

	auto screen = ScreenInteractive::FullscreenAlternateScreen();
	auto ui_alive = std::make_shared<std::atomic<bool>>(true);

	auto root = CatchEvent(
		Renderer(
			input,
			[&] {
				const bool copy_feedback_active =
					std::chrono::steady_clock::now() < copy_feedback_until;

				visible_matches = FilterHostMatches(hosts, query);
				ClampSelection(selected,
							   static_cast<int>(visible_matches.size()));
				AdjustScrollOffset(scroll_offset, selected,
								   static_cast<int>(visible_matches.size()),
								   kMaxVisibleRows);

				Elements rows;
				rows.reserve(kMaxVisibleRows);

				const int window_start = scroll_offset;
				const int window_end =
					std::min(window_start + kMaxVisibleRows,
							 static_cast<int>(visible_matches.size()));

				for (int i = window_start; i < window_end; ++i) {
					const HostMatch &match =
						visible_matches[static_cast<size_t>(i)];
					const Host &host = hosts[match.index];
					const bool is_selected = i == selected;

					auto indicator =
						host.isActive
							? (text("*") | bold | color(kActiveIndicator))
							: text("");
					auto alias =
						hbox({
							RenderAliasWithMatches(host.alias, match.positions),
							indicator,
						}) |
						size(WIDTH, EQUAL, alias_column_width);

					const std::string user =
						host.user.empty() ? "-" : host.user;
					const std::string hostname =
						host.hostname.empty() ? "-" : host.hostname;

					auto identity = hbox({
						text(user) | color(kUserColor),
						text("@"),
						text(hostname) | color(kHostColor),
					});
					Element row = hbox({
									  text(" "),
									  alias,
									  text(std::string(kAliasIdentityGap, ' ')),
									  identity,
									  text(" "),
								  }) |
								  xflex;
					if (is_selected) {
						row = row |
							  bgcolor(copy_feedback_active ? kCopiedRowBg
														  : kSelectedRowBg) |
							  color(kSelectedRowFg);
					}
					rows.push_back(row);
				}

				if (rows.empty()) {
					rows.push_back(hbox({text(" No hosts available ") | dim}));
				}

				const std::string result_counter =
					"[" + std::to_string(visible_matches.size()) + "/" +
					std::to_string(hosts.size()) + "]";
				const int search_query_width =
					std::max(1, picker_width - 3 -
									static_cast<int>(result_counter.size()));

				Element picker =
					vbox({
							hbox({
								text("> ") | color(kSearchPromptColor),
								RenderSearchQuery(query, input_cursor_position,
											  search_query_width),
								text(" "),
								text(result_counter) | dim,
							}),
						vbox(std::move(rows)) |
							size(HEIGHT, EQUAL, kMaxVisibleRows) |
							borderRounded,
					}) |
					size(WIDTH, EQUAL, picker_width) | hcenter;

				Element picker_slot = vbox({picker, filler()}) |
									  size(HEIGHT, EQUAL, max_picker_height);

				return picker_slot | hcenter | vcenter | flex;
			}),
		[&](Event event) {
			if (event == Event::Custom) {
				return true;
			}

			if (event == Event::Character('q') || event == Event::CtrlC) {
				screen.Exit();
				return true;
			}

			if (event == Event::Escape) {
				query.clear();
				input_cursor_position = 0;
				selected = 0;
				scroll_offset = 0;
				return true;
			}

			if (event == Event::ArrowUp) {
				if (!visible_matches.empty()) {
					selected = std::max(0, selected - 1);
				}
				return true;
			}

			if (event == Event::ArrowDown) {
				if (!visible_matches.empty()) {
					selected =
						std::min(selected + 1,
								 static_cast<int>(visible_matches.size()) - 1);
				}
				return true;
			}

			if (event == Event::Return) {
				if (!visible_matches.empty()) {
					const Host &host = hosts[visible_matches[selected].index];
					LaunchTmuxSession(host);
				}
				screen.Exit();
				return true;
			}

			if (event == Event::CtrlY) {
				if (!visible_matches.empty()) {
					const Host &host = hosts[visible_matches[selected].index];
					CopyIpToClipboard(host);
					copy_feedback_until =
						std::chrono::steady_clock::now() +
						std::chrono::milliseconds(150);

					auto ui_alive_ref = ui_alive;
					std::thread([&screen, ui_alive_ref] {
						std::this_thread::sleep_for(std::chrono::milliseconds(150));
						if (ui_alive_ref->load()) {
							screen.PostEvent(Event::Custom);
						}
					}).detach();
				}
				//screen.Exit();
				return true;
			}

			return false;
		});

	screen.Loop(root);
	ui_alive->store(false);
	return 0;
}
