#include "parser.h"
#include "tmux.h"
#include "types.h"
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
	try {
		std::vector<Host> hosts = ParseSSHConfig(SSH_CONFIG_PATH);

		printHostTable(hosts);
		// LaunchTmuxSession(hosts[0]);
		UpdateHostsStatus(hosts);
		std::cout << std::endl;
		printHostTable(hosts);
	} catch (const std::exception &e) {
		std::cerr << "Unhandled exception: " << e.what() << std::endl;
	}

	return 0;
}
