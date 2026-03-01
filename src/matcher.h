#ifndef MATCHER_H
#define MATCHER_H
#include "types.h"
#include <string>
#include <vector>

struct HostMatch {
	int index;
	int score;
	std::vector<int> positions;
};

std::vector<HostMatch> RankHosts(const std::vector<Host> &hosts,
								 const std::string &query);
#endif
