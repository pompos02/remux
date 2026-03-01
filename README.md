# remux

`ssh-tui` is a small terminal UI that helps you jump to SSH hosts defined in `~/.ssh/config`.
It presents your host aliases in a searchable picker, then opens or switches to a matching tmux session and runs `ssh <alias>`.

## Scope

- Parses SSH host entries from `~/.ssh/config`
- Shows them in an interactive FTXUI picker with fuzzy alias matching
- Marks hosts that already have an active tmux session
- On Enter, attaches/switches to a tmux session for the selected host
- On `Ctrl+Y`, copies the selected host's hostname to the clipboard (`clip.exe` only for wsl)


## Dependencies

- C++17 compiler
- FTXUI (`ftxui-component`, `ftxui-dom`, `ftxui-screen`)
- `tmux`
