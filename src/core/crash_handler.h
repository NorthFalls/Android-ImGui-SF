#pragma once

namespace sf {

// Installs SIGSEGV/SIGABRT/SIGILL/SIGBUS/SIGFPE handlers that append a
// backtrace to the given path before letting the process die normally.
void installCrashHandler(const char* path);

}  // namespace sf
