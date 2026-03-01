#include "ui.h"

#include "matcher.h"
#include "tmux.h"
#include "types.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace {

const Color kMatchFg = Color::RGB(230, 236, 245);
const Color kMatchBg = Color::RGB(72, 93, 120);
const Color kActiveIndicator = Color::RGB(123, 182, 148);
const Color kUserColor = Color::RGB(197, 188, 164);
const Color kHostColor = Color::RGB(146, 172, 204);
const Color kSelectedRowBg = Color::RGB(41, 46, 56);
constexpr int kAliasIdentityGap = 5;

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

		const int row_content_width =
			1 + alias_column_width + kAliasIdentityGap + user_width + 1 +
			host_width + 1;
		max_row_content_width = std::max(max_row_content_width, row_content_width);
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
						  const std::string &placeholder, int width) {
	if (width <= 0) {
		return text("");
	}

	if (query.empty()) {
		const int placeholder_width = std::max(0, width - 1);
		const std::string placeholder_visible =
			placeholder.substr(0, static_cast<size_t>(placeholder_width));
		const int trailing_space_count =
			placeholder_width - static_cast<int>(placeholder_visible.size());

		Elements parts = {
			text("|") | dim,
			text(placeholder_visible) | dim,
		};
		if (trailing_space_count > 0) {
			parts.push_back(text(std::string(trailing_space_count, ' ')));
		}
		return hbox(std::move(parts));
	}

	const int safe_cursor =
		std::max(0, std::min(cursor_position, static_cast<int>(query.size())));
	const int window_start =
		std::max(0, std::min(safe_cursor - width + 1,
							 static_cast<int>(query.size())));
	const int visible_len =
		std::min(std::max(0, width - 1),
				 static_cast<int>(query.size()) - window_start);
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
	parts.push_back(text("|") | dim);
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

} // namespace

int RunHostPickerUI(std::vector<Host> &hosts) {
	std::string query;
	int input_cursor_position = 0;
	int selected = 0;
	std::vector<HostMatch> visible_matches = FilterHostMatches(hosts, query);
	const int alias_column_width = ComputeAliasColumnWidth(hosts);
	const int picker_width = ComputePickerWidth(hosts, alias_column_width);
	const int max_list_height = std::max(1, static_cast<int>(hosts.size()));
	const int max_picker_height = max_list_height + 3;

	InputOption input_option;
	input_option.placeholder = "Search hosts...";
	input_option.cursor_position = &input_cursor_position;
	input_option.multiline = false;
	Component input = Input(&query, input_option);

	auto screen = ScreenInteractive::FullscreenAlternateScreen();

	auto root = CatchEvent(
		Renderer(
			input,
			[&] {
				visible_matches = FilterHostMatches(hosts, query);
				ClampSelection(selected,
							   static_cast<int>(visible_matches.size()));

				Elements rows;
				rows.reserve(visible_matches.size());

				for (size_t i = 0; i < visible_matches.size(); ++i) {
					const HostMatch &match = visible_matches[i];
					const Host &host = hosts[match.index];
					const bool is_selected = static_cast<int>(i) == selected;

					auto indicator =
						host.isActive
							? (text("*") | bold | color(kActiveIndicator))
							: text("");
					auto alias = hbox({
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
						text("@") | dim,
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
						row = row | bgcolor(kSelectedRowBg);
					}
					rows.push_back(row);
				}

				if (rows.empty()) {
					rows.push_back(hbox({text(" No hosts available ") | dim}));
				}

				Element picker =
					vbox({
						hbox({
							text("> ") | dim,
							RenderSearchQuery(query, input_cursor_position,
											  input_option.placeholder(),
											  picker_width - 2),
						}),
						vbox(std::move(rows)) | borderRounded,
					}) |
					size(WIDTH, EQUAL, picker_width) | hcenter;

				Element picker_slot = vbox({picker, filler()}) |
									  size(HEIGHT, EQUAL, max_picker_height);

				return picker_slot | hcenter | vcenter | flex;
			}),
		[&](Event event) {
			if (event == Event::Character('q') || event == Event::CtrlC) {
				screen.Exit();
				return true;
			}

			if (event == Event::Escape) {
				query.clear();
				input_cursor_position = 0;
				selected = 0;
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

			return false;
		});

	screen.Loop(root);
	return 0;
}
