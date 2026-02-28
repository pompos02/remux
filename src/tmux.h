#ifndef TMUX_H
#define TMUX_H
#include "types.h"
#include <vector>

void LaunchTmuxSession(const Host& host);
void UpdateHostsStatus(std::vector<Host> &hosts);

#endif
