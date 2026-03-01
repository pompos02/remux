#include "parser.h"
#include "tmux.h"
#include "debug.h"
#include "types.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

void printHostTable(const std::vector<Host> &hosts) {
	const char *WhoIami = "printHostTable";
	WriteDebug("Entering*%s\n", WhoIami);
	if (hosts.empty()) {
		WriteDebug("No hosts found.\n");
		return;
	}

	for (const auto &host : hosts) {
		WriteDebug("Host*[%s][%s][%s][%s]\n",
				   (host.alias.empty() ? "-" : host.alias.c_str()),
				   (host.hostname.empty() ? "-" : host.hostname.c_str()),
				   (host.user.empty() ? "-" : host.user.c_str()),
				   (host.isActive ? "true" : "false"));
	}
	WriteDebug("Exiting*%s\n", WhoIami);
}

/* Populate the hosts vector */
void populateHosts(std::vector<Host> &hosts,
				   std::vector<std::string> currentAliases,
				   std::map<std::string, std::string> currentFields) {
	if (!currentAliases.empty()) {
		for (const auto &alias : currentAliases) {
			Host entry;
			entry.alias = alias;
			entry.hostname = currentFields["HostName"];
			entry.user = currentFields["User"];
			hosts.push_back(entry);
		}
	}
}

/* Trim whitespace from both ends */
void trim(std::string &str) {
	size_t first = str.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		str.clear(); // String is all whitespace
		return;
	}

	size_t last = str.find_last_not_of(" \t\r\n");
	str.erase(last + 1);
	str.erase(0, first);
}

/* Expand "~" to HOME dir */
std::string expandHome(const std::string &path) {
	if (path.size() > 0 && path[0] == '~') {
		const char *home = std::getenv("HOME");
		if (home) {
			return std::string(home) + path.substr(1);
		}
	}
	return path;
}

void removeComments(std::string &line) {
	size_t commetPos = line.find('#');
	if (commetPos != std::string::npos) {
		line.erase(commetPos);
	}
}

std::vector<Host> ParseSSHConfig(const std::string &path) {
	std::string configPath = expandHome(path);
	std::ifstream file(configPath);

	if (!file) {
		const int err = errno;
		std::string msg = "Failed to open up SSH config: " + configPath;
		if (errno != 0) {
			if (err != 0) {
				msg += " (";
				msg += std::strerror(err);
				msg += ")";
			}
			throw std::runtime_error(msg);
		}
	}

	std::vector<Host> hosts;
	std::vector<std::string> currentAliases;
	std::map<std::string, std::string> currentFields;

	std::string line;

	while (std::getline(file, line)) {
		trim(line);
		removeComments(line);
		if (line.empty())
			continue;

		std::istringstream iss(line);
		std::string key;
		iss >> key;

		if (key == "Host") {
			populateHosts(hosts, currentAliases, currentFields);
			// Start a new Host Block
			currentAliases.clear();
			currentFields.clear();

			std::string alias;
			while (iss >> alias && alias != "*") {
				currentAliases.push_back(alias);
			}
		} else {
			std::string value;
			std::getline(iss, value);
			trim(value);
			currentFields[key] = value;
		}
	}
	// Save the last block
	populateHosts(hosts, currentAliases, currentFields);
	UpdateHostsStatus(hosts);
	return hosts;
}
