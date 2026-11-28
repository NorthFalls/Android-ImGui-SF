#pragma once

namespace sf {

// Installs SIGSEGV/SIGABRT/SIGILL/SIGBUS/SIGFPE handlers that append a
// backtrace to the given path before letting the process die normally.
void installCrashHandler(const char* path);

// Records the startup/render stage the process is currently in. A signal handler
// cannot safely call into most of the runtime, but it can copy this label into
// the crash record, which tells "died in EGL surface creation" from "died while
// drawing frame 3" without any symbolization.
void setCrashStage(const char* stage);

}  // namespace sf
