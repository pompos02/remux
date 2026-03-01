#include "debug.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
void CopyIpToClipboard(const Host &host) {
	std::string cmd = "clip.exe";

	std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(cmd.c_str(), "w"),
												(int (*)(FILE *))pclose);
	if (!pipe) {
		WriteError("popen() failed\n");
		throw std::runtime_error("popen() failed" +
								 std::string(std::strerror(errno)));
	}

	size_t size = host.hostname.size();
	if (fwrite(host.hostname.c_str(), sizeof(char), size, pipe.get())) {
		WriteError("fwrite() failed to write all data\n");
	}
}
