#if DEBUG
#include "debug.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>

void WriteDebug(const char *fmt, ...) {
	char message[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	std::time_t now = std::time(nullptr);
	std::tm tm_now{};
	localtime_r(&now, &tm_now);

	char ts[32];
	std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

	std::error_code ec;
	std::filesystem::create_directories("logs", ec);

	FILE *log_file = std::fopen("logs/main.dmp", "a");
	if (log_file != nullptr) {
		std::fprintf(log_file, "[%s] [DEBUG] %s", ts, message);
		std::fclose(log_file);
	}
}

void WriteError(const char *fmt, ...) {
	char message[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	std::time_t now = std::time(nullptr);
	std::tm tm_now{};
	localtime_r(&now, &tm_now);

	char ts[32];
	std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

	std::error_code ec;
	std::filesystem::create_directories("logs", ec);

	FILE *log_file = std::fopen("logs/main.dmp", "a");
	if (log_file != nullptr) {
		std::fprintf(log_file, "[%s] [ERROR] %s", ts, message);
		std::fclose(log_file);
	}
}
#endif
