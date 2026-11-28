#include "core/crash_handler.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <unwind.h>

namespace sf {
namespace {

std::atomic<const char*> g_stage{nullptr};

int g_fd = -1;

struct Buffer {
    char data[8192];
    size_t length = 0;
};

void append(Buffer& buffer, const char* text) {
    const size_t length = std::strlen(text);
    if (buffer.length + length >= sizeof(buffer.data)) return;
    std::memcpy(buffer.data + buffer.length, text, length);
    buffer.length += length;
}

void appendUnsigned(Buffer& buffer, uintptr_t value) {
    char digits[32];
    int count = 0;
    if (value == 0) digits[count++] = '0';
    while (value != 0) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    if (buffer.length + static_cast<size_t>(count) >= sizeof(buffer.data)) return;
    while (count-- > 0) buffer.data[buffer.length++] = digits[count];
}

void appendHex(Buffer& buffer, uintptr_t value) {
    static const char* kHex = "0123456789abcdef";
    char digits[2 * sizeof(uintptr_t)];
    int count = 0;
    if (value == 0) digits[count++] = '0';
    while (value != 0) {
        digits[count++] = kHex[value & 0xF];
        value >>= 4;
    }
    append(buffer, "0x");
    if (buffer.length + static_cast<size_t>(count) >= sizeof(buffer.data)) return;
    while (count-- > 0) buffer.data[buffer.length++] = digits[count];
}

struct TraceState {
    uintptr_t frames[48];
    int count = 0;
};

_Unwind_Reason_Code traceCallback(_Unwind_Context* context, void* argument) {
    auto* state = static_cast<TraceState*>(argument);
    const uintptr_t ip = _Unwind_GetIP(context);
    if (ip != 0 && state->count < 48) state->frames[state->count++] = ip;
    return state->count >= 48 ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void handler(int signalNumber, siginfo_t* info, void* context) {
    (void)context;
    if (g_fd >= 0) {
        Buffer buffer{};
        append(buffer, "\n--- native crash ---\nsignal ");
        appendUnsigned(buffer, static_cast<uintptr_t>(signalNumber));
        if (info != nullptr) {
            append(buffer, " at ");
            appendHex(buffer, reinterpret_cast<uintptr_t>(info->si_addr));
        }
        const char* stage = g_stage.load(std::memory_order_relaxed);
        append(buffer, "\nstage ");
        append(buffer, stage != nullptr ? stage : "unknown");
        append(buffer, "\n");

        TraceState state{};
        _Unwind_Backtrace(traceCallback, &state);
        for (int i = 0; i < state.count; ++i) {
            append(buffer, "  #");
            appendUnsigned(buffer, static_cast<uintptr_t>(i));
            append(buffer, " ");
            appendHex(buffer, state.frames[i]);
            append(buffer, "\n");
        }
        append(buffer, "--- end ---\n");

        const ssize_t written = ::write(g_fd, buffer.data, buffer.length);
        (void)written;
        ::fsync(g_fd);
        // Mirror the record on the terminal that started the host. Binary mode is
        // diagnosed from that terminal, and the log file may never leave the phone.
        const ssize_t shown = ::write(STDERR_FILENO, buffer.data, buffer.length);
        (void)shown;
    }

    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

}  // namespace

void installCrashHandler(const char* path) {
    if (path == nullptr || *path == '\0') return;
    if (g_fd >= 0) return;
    // A crash inside the unwinder, or on an exhausted stack, would leave the
    // handler unable to run at all: give the signal its own stack.
    static char altStackMemory[64 * 1024];
    stack_t altStack = {};
    altStack.ss_sp = altStackMemory;
    altStack.ss_size = sizeof(altStackMemory);
    ::sigaltstack(&altStack, nullptr);
    g_fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (g_fd < 0) return;

    struct sigaction action {};
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    ::sigaction(SIGSEGV, &action, nullptr);
    ::sigaction(SIGABRT, &action, nullptr);
    ::sigaction(SIGILL, &action, nullptr);
    ::sigaction(SIGBUS, &action, nullptr);
    ::sigaction(SIGFPE, &action, nullptr);
}

void setCrashStage(const char* stage) {
    // Relaxed is enough: this is a label, and a slightly stale stage in the crash
    // text is still far better than "unknown".
    g_stage.store(stage, std::memory_order_relaxed);
}

}  // namespace sf
