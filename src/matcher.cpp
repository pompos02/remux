#include "matcher.h"
#include "debug.h"
#include "types.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

struct MatchResult {
	int score;
	std::vector<int> positions;
};

static inline char low(char c) { return (char)std::tolower((unsigned char)c); }

bool isBoundaryChar(char c) {
	return c == '/' || c == '\\' || c == '_' || c == '-' || c == ' ' ||
		   c == '.';
}

bool isCamelBoundary(char prev, char cur) {
	return std::islower((unsigned char)prev) &&
		   std::isupper((unsigned char)cur);
}

std::string ToLowerAscii(std::string s) {
	for (char &c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

int boundaryBonus(const std::string &t, int j) {
	if (j == 0)
		return 19; // string prefix bonus
	char prev = t[j - 1];
	char cur = t[j];
	if (isBoundaryChar(prev))
		return 15;
	if (isCamelBoundary(prev, cur))
		return 10;
	return 0;
}

MatchResult fuzzyMatch(const std::string &query, const std::string &target) {
	const int NEG = std::numeric_limits<int>::min() / 4;

	int m = query.size();
	int n = target.size();

	if (m == 0)
		return {0, {}};

	/* best score for matching query[0..i] ending exactly at target[j] */
	std::vector<std::vector<int>> dp(m, std::vector<int>(n, NEG));
	/* stores prior target index to reconstruct matched positions */
	std::vector<std::vector<int>> parent(m, std::vector<int>(n, -1));

	// First row
	for (int j = 0; j < n; ++j) {
		if (low(query[0]) == low(target[j])) {
			dp[0][j] = 10 - j + boundaryBonus(target, j);
		}
	}

	for (int i = 1; i < m; ++i) {
		for (int j = 0; j < n; ++j) {
			if (low(query[i]) != low(target[j]))
				continue;

			for (int k = 0; k < j; ++k) {
				if (dp[i - 1][k] == NEG)
					continue;

				int score = dp[i - 1][k] + 10;

				if (k + 1 == j)
					score += 15; // consecutive bonus
				else
					score -= (j - k - 1); // gap penalty

				score += boundaryBonus(target, j);

				if (score > dp[i][j]) {
					dp[i][j] = score;
					parent[i][j] = k;
				}
			}
		}
	}

	// Find best final match
	int bestScore = NEG;
	int bestPos = -1;

	for (int j = 0; j < n; ++j) {
		if (dp[m - 1][j] > bestScore) {
			bestScore = dp[m - 1][j];
			bestPos = j;
		}
	}

	if (bestScore == NEG)
		return {NEG, {}};

	// Backtrack positions
	std::vector<int> positions(m);
	int j = bestPos;
	for (int i = m - 1; i >= 0; --i) {
		positions[i] = j;
		j = parent[i][j];
	}

	return {bestScore, positions};
}

bool BetterCandidate(const HostMatch &lhs, const HostMatch &rhs,
					 const std::vector<Host> &hosts) {
	if (lhs.score != rhs.score)
		return lhs.score > rhs.score;

	const std::string l_alias = ToLowerAscii(hosts[lhs.index].alias);
	const std::string r_alias = ToLowerAscii(hosts[rhs.index].alias);
	if (l_alias != r_alias)
		return l_alias < r_alias;

	return lhs.index < rhs.index;
}

std::vector<HostMatch> RankHosts(const std::vector<Host> &hosts,
								 const std::string &query) {
	const std::string q = ToLowerAscii(query);
	std::vector<HostMatch> ranked;
	ranked.reserve(hosts.size());

	if (q.empty()) {
		for (size_t i = 0; i < hosts.size(); ++i) {
			ranked.push_back(HostMatch{static_cast<int>(i), 0, {}});
		}
		return ranked;
	}

	for (size_t i = 0; i < hosts.size(); ++i) {
		const std::string alias = hosts[i].alias;
		const std::string lalias = ToLowerAscii(alias);
		const MatchResult result = fuzzyMatch(q, lalias);

		WriteDebug("Host[%s], score[%d]\n", alias.c_str(), result.score);
		for (size_t p = 0; p < result.positions.size(); ++p) {
			WriteDebug(
				"Host[%s], query_index[%zu], alias_index[%d], query_char[%c], "
				"alias_char[%c]\n",
				alias.c_str(), p, result.positions[p], query[p],
				alias[result.positions[p]]);
		}

		if (result.score == std::numeric_limits<int>::min() / 4) {
			continue;
		}

		ranked.push_back(
			HostMatch{static_cast<int>(i), result.score, result.positions});
	}

	std::sort(ranked.begin(), ranked.end(),
			  [&](const HostMatch &lhs, const HostMatch &rhs) {
				  return BetterCandidate(lhs, rhs, hosts);
			  });

	for (size_t i = 0; i < ranked.size(); ++i) {
		const Host &host = hosts[ranked[i].index];
		WriteDebug("Rank[%zu], Host[%s], score[%d]\n", i + 1,
				   host.alias.c_str(), ranked[i].score);
	}

	return ranked;
}
