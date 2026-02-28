#include "parser.h"
#include "types.h"
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

void printHostTable(const std::vector<Host> &hosts) {
	if (hosts.empty()) {
		std::cout << "No hosts found." << std::endl;
		return;
	}

	// Define column widths
	const int aliasWidth = 15;
	const int hostWidth = 30;
	const int userWidth = 15;
	const int activeWidth = 10;

	// Print Header
	std::cout << std::left << std::setw(aliasWidth) << "ALIAS"
			  << std::setw(hostWidth) << "HOSTNAME" << std::setw(userWidth)
			  << "USER" << std::setw(activeWidth) << "ACTIVE"
			  << std::endl;

	// Print Separator Line
	std::cout << std::string(aliasWidth + hostWidth + userWidth + activeWidth,
							 '-')
			  << std::endl;

	// Print Rows
	for (const auto &host : hosts) {
		std::cout << std::left << std::setw(aliasWidth)
				  << (host.alias.empty() ? "-" : host.alias)
				  << std::setw(hostWidth) << host.hostname
				  << std::setw(userWidth) << host.user
				  << std::setw(activeWidth)
				  << (host.isActive ? "true" : "false") << std::endl;
	}
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
		std::cerr << "Failed to open ssh config File\n";
		exit(1);
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
	return hosts;
}
