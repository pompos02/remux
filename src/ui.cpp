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

std::vector<HostMatch> FilterHostMatches(const std::vector<Host> &hosts,
									 const std::string &query) {
	return RankHosts(hosts, query);
}

int ComputePickerWidth(const std::vector<Host> &hosts) {
	constexpr int kMinPickerWidth = 72;
	constexpr int kMaxPickerWidth = 110;

	int max_row_content_width = 0;
	for (const Host &host : hosts) {
		const int alias_width =
			static_cast<int>(host.alias.empty() ? 1 : host.alias.size());
		const int user_width =
			static_cast<int>(host.user.empty() ? 1 : host.user.size());
		const int host_width =
			static_cast<int>(host.hostname.empty() ? 1 : host.hostname.size());

		const int row_content_width =
			1 + alias_width + 1 + 2 + user_width + 1 + host_width + 1;
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
			text(" ") | underlined,
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
		std::min(width, static_cast<int>(query.size()) - window_start);
	const std::string visible =
		query.substr(window_start, static_cast<size_t>(visible_len));
	const int cursor_in_visible = safe_cursor - window_start;

	Elements parts;
	if (cursor_in_visible >= visible_len) {
		parts.push_back(text(visible));
		parts.push_back(text(" ") | underlined);
		const int trailing_space_count = width - visible_len - 1;
		if (trailing_space_count > 0) {
			parts.push_back(text(std::string(trailing_space_count, ' ')));
		}
		return hbox(std::move(parts));
	}

	const std::string left = visible.substr(0, static_cast<size_t>(cursor_in_visible));
	const std::string cursor_char =
		visible.substr(static_cast<size_t>(cursor_in_visible), 1);
	const std::string right = visible.substr(static_cast<size_t>(cursor_in_visible + 1));
	const int rendered_len = static_cast<int>(visible.size());
	const int trailing_space_count = width - rendered_len;

	parts.push_back(text(left));
	parts.push_back(text(cursor_char) | underlined);
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
	const int picker_width = ComputePickerWidth(hosts);

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
							: text(" ");
					auto alias =
						RenderAliasWithMatches(host.alias, match.positions) | xflex;

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
								  indicator | size(WIDTH, EQUAL, 1),
								  text("  "),
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
						vbox({
							hbox({
								text(" "),
								RenderSearchQuery(query, input_cursor_position,
												  input_option.placeholder(),
												  picker_width - 2),
								text(" "),
							}),
							separator() | dim,
						}),
						text(""),
						vbox(std::move(rows)) | borderRounded,
					}) |
					size(WIDTH, EQUAL, picker_width) | hcenter;

				return picker | hcenter | vcenter | flex;
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
