#include "ui.h"

#include "tmux.h"
#include "types.h"

#include <algorithm>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace {

std::vector<int> FilterHostIndices(const std::vector<Host> &hosts,
                                   const std::string &query) {
  (void)query;

  // Placeholder for fuzzy filtering logic.
  std::vector<int> indices;
  indices.reserve(hosts.size());
  for (size_t i = 0; i < hosts.size(); ++i) {
    indices.push_back(static_cast<int>(i));
  }
  return indices;
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
  std::vector<int> visible_indices = FilterHostIndices(hosts, query);

  InputOption input_option;
  input_option.placeholder = "Search hosts...";
  Component input = Input(&query, input_option);

  auto screen = ScreenInteractive::TerminalOutput();

  auto root = CatchEvent(
      Renderer(input, [&] {
        visible_indices = FilterHostIndices(hosts, query);
        ClampSelection(selected, static_cast<int>(visible_indices.size()));

        Elements rows;
        rows.reserve(visible_indices.size());

        for (size_t i = 0; i < visible_indices.size(); ++i) {
          const Host &host = hosts[visible_indices[i]];
          const bool is_selected = static_cast<int>(i) == selected;

          auto indicator = text(host.isActive ? "[A] " : "[ ] ");
          auto alias = text(host.alias.empty() ? "-" : host.alias);
          auto hostname =
              text(host.hostname.empty() ? "" : "  " + host.hostname) | dim;

          Element row = hbox({indicator, alias, hostname});
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
                   text("Enter: connect   Up/Down: navigate   Esc: clear   q: quit") |
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
          if (!visible_indices.empty()) {
            selected = std::max(0, selected - 1);
          }
          return true;
        }

        if (event == Event::ArrowDown) {
          if (!visible_indices.empty()) {
            selected =
                std::min(selected + 1, static_cast<int>(visible_indices.size()) - 1);
          }
          return true;
        }

        if (event == Event::Return) {
          if (!visible_indices.empty()) {
            const Host &host = hosts[visible_indices[selected]];
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
