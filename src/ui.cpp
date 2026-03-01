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
			ch = ch | bold | color(Color::Cyan);
		}
		letters.push_back(ch);
	}

	return hbox(std::move(letters));
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
	int selected = 0;
	std::vector<HostMatch> visible_matches = FilterHostMatches(hosts, query);

	InputOption input_option;
	input_option.placeholder = "Search hosts...";
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

					auto indicator = text(host.isActive ? "[A] " : "[ ] ");
					auto alias =
						RenderAliasWithMatches(host.alias, match.positions);
					auto hostname =
						text(host.hostname.empty() ? ""
												   : "  " + host.hostname) |
						dim;
					auto score =
						query.empty()
							? text("")
							: (text("  [" + std::to_string(match.score) + "]") |
							   dim);

					Element row = hbox({indicator, alias, hostname, score});
					if (is_selected) {
						row = row | inverted;
					}
					rows.push_back(row);
				}

				if (rows.empty()) {
					rows.push_back(text("No hosts available") | dim);
				}

				return vbox({
						   hbox({text("> "), input->Render()}),
						   separator(),
						   text("Enter: connect   Up/Down: navigate   Esc: "
								"clear   q: quit") |
							   dim,
						   separator(),
						   vbox(std::move(rows)),
					   }) |
					   border;
			}),
		[&](Event event) {
			if (event == Event::Character('q') || event == Event::CtrlC) {
				screen.Exit();
				return true;
			}

			if (event == Event::Escape) {
				query.clear();
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
