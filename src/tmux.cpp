#include "types.h"
#include "debug.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

bool isSessionFound(const std::vector<std::string> &sessions,
					const std::string target) {
	// If find doesn't reach the end, it means the string was found
	return std::find(sessions.begin(), sessions.end(), target) !=
		   sessions.end();
}

std::vector<std::string> getTmuxSesssions() {
	std::vector<std::string> sessions;
	std::string cmd = "tmux ls -F \"#{session_name}\" 2>/dev/null";

	// Open a pipe to the command
	std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(cmd.c_str(), "r"),
												(int (*)(FILE *))pclose);

	if (!pipe) {
		WriteError("popen() failed\n");
		throw std::runtime_error("popen() failed" + std::string(std::strerror(errno)));
	}

	// Read the ouptut line by line
	char line[256];
	while (fgets(line, sizeof(line), pipe.get()) != nullptr) {
		std::string session_name(line);

		// Remove the trainling \n
		if (!session_name.empty() && session_name.back() == '\n')
			session_name.pop_back();

		if (!session_name.empty())
			sessions.push_back(session_name);
	}
	return sessions;
}

void LaunchTmuxSession(const Host &host) {
	bool isInsideTmux = std::getenv("TMUX") != nullptr;
	std::string innerCmd = "ssh " + host.alias + "; exec \\$SHELL -l";

	std::string cmd;
	if (isInsideTmux) {
		// 1) Check if session exists (has-session)
		// 2) If it doesn't (||), create it in the background (-d)
		// 3) Switch the current client to that session (switch-client)
		cmd = "tmux has-session -t '" + host.alias + "' 2>/dev/null || " +
			  "tmux new-session -d -s '" + host.alias + "' \"" + innerCmd +
			  "\"; " + "tmux switch-client -t '" + host.alias + "'";
	} else {
		cmd =
			"tmux new-session -A -s '" + host.alias + "' \"" + innerCmd + "\"";
	}

	std::system(cmd.c_str());
}

void UpdateHostsStatus(std::vector<Host> &hosts) {
	std::vector<std::string> sessions;
	sessions = getTmuxSesssions();
	for (auto &h : hosts) {
		if (isSessionFound(sessions, h.alias))
			h.isActive = true;
	}
}
