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
						  const std::string &placeholder) {
	if (query.empty()) {
		return hbox({text(" ") | underlined, text(placeholder) | dim});
	}

	const int safe_cursor =
		std::max(0, std::min(cursor_position, static_cast<int>(query.size())));
	const std::string left = query.substr(0, safe_cursor);
	const std::string right = query.substr(safe_cursor);

	if (right.empty()) {
		return hbox({text(left), text(" ") | underlined});
	}

	return hbox({
		text(left),
		text(std::string(1, right.front())) | underlined,
		text(right.substr(1)),
	});
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

	InputOption input_option;
	input_option.placeholder = "Search hosts...";
	input_option.cursor_position = &input_cursor_position;
	input_option.multiline = false;
	Component input = Input(&query, input_option);

	auto screen = ScreenInteractive::TerminalOutput();

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
						RenderAliasWithMatches(host.alias, match.positions) |
						xflex;

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
									  indicator,
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
								// text("Search ") | dim,
								RenderSearchQuery(query, input_cursor_position,
												  input_option.placeholder()) |
									xflex,
								text(" "),
							}),
							separator() | dim,
						}),
						text(""),
						vbox(std::move(rows)) | borderRounded,
					}) |
					size(WIDTH, LESS_THAN, 96) | hcenter;

				Element hints = hbox({
									text("Enter") | dim,
									text(": connect   "),
									text("Up/Down") | dim,
									text(": navigate   "),
									text("Esc") | dim,
									text(": clear   "),
									text("q") | dim,
									text(": quit"),
								}) |
								dim | hcenter;

				return vbox({
					filler(),
					picker,
					filler(),
					hints,
				});
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
