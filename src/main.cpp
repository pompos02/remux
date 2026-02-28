#include "debug.h"
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
		UpdateHostsStatus(hosts);
		printHostTable(hosts);

	} catch (const std::exception &e) {
		WriteError("Fatal error: unknown exception\n");
		std::cerr << "Unhandled exception: unknown error" << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
