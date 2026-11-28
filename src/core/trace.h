#pragma once

namespace sf {

// Lightweight startup trace. Writes a few lines to a text file so that a crash
// before or during engine start leaves a readable breadcrumb trail on disk.
// Passing an empty or null path disables tracing entirely.
void traceInit(const char* path);
void trace(const char* format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace sf
