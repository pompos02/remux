#ifndef PARSER_H
#define PARSER_H
#include <string>
#include <vector>
#include "types.h"

#define SSH_CONFIG_PATH "~/.ssh/config"

std::vector<Host> ParseSSHConfig(const std::string &path);
void printHostTable(const std::vector<Host>& hosts);
#endif
