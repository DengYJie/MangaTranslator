#include "core/DebugOutputSink.h"

namespace manga::core {

void writeDebugOutput(const QString&) {
    // No debugger output channel is wired for this platform, so the standard error mirror is the
    // only out-of-process destination besides the log file.
}

} // namespace manga::core
