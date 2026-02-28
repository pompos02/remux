#ifndef TYPES_H
#define TYPES_H
#include <string>

struct Host {
	std::string alias;
	std::string hostname;
	std::string user;
	bool isActive{false};
};

#endif
