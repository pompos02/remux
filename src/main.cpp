#include "debug.h"
#include "parser.h"
#include "types.h"
#include "ui.h"
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
	try {
		WriteDebug("\n");
		std::vector<Host> hosts = ParseSSHConfig(SSH_CONFIG_PATH);
		return RunHostPickerUI(hosts);

	} catch (const std::exception &e) {
		WriteError("Fatal error: unknown exception\n");
		std::cerr << "Unhandled exception: unknown error" << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}
