#ifndef DEBUG_H
#define DEBUG_H

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
void WriteDebugImpl(const char *fmt, ...);
void WriteErrorImpl(const char *fmt, ...);
#define WriteDebug(...) WriteDebugImpl(__VA_ARGS__)
#define WriteError(...) WriteErrorImpl(__VA_ARGS__)
#else
#define WriteDebug(...) ((void)0)
#define WriteError(...) ((void)0)
#endif

#endif
