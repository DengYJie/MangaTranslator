#include "core/DebugOutputSink.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace manga::core {

void writeDebugOutput(const QString& line) {
    // OutputDebugStringW is the single channel that Visual Studio's Output window and Sysinternals
    // DebugView both read, so a GUI subsystem build reaches the IDE without owning a console.
    const QString terminated = line + QChar(u'\n');
    ::OutputDebugStringW(reinterpret_cast<LPCWSTR>(terminated.utf16()));
}

} // namespace manga::core
