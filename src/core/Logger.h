#pragma once

#include "core/DebugOutputSink.h"

#include <QString>

namespace manga::core {

/// Destination for a formatted log line mirrored outside the log file.
using DebugSink = void (*)(const QString& line);

/// Initializes the core logging subsystem.
/// Sets up a file sink in the specified directory and installs a custom Qt message handler that
/// mirrors each formatted line to standard error and to debugSink. The default sink writes to the
/// attached debugger; a test supplies its own to observe delivery.
void initLogger(const QString& logDirPath, DebugSink debugSink = &writeDebugOutput);

/// Shuts down the logging subsystem and cleans up the message handler.
void shutdownLogger();

} // namespace manga::core
