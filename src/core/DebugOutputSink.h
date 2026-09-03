#pragma once

#include <QString>

namespace manga::core {

/// Mirrors one already formatted log line to the debugger output channel of the host platform.
/// The line is delivered as given, including any embedded newlines. When no debugger is attached
/// the call returns immediately and produces no output.
void writeDebugOutput(const QString& line);

} // namespace manga::core
