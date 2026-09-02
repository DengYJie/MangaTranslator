#pragma once

#include <QString>

namespace manga::core {

/// Initializes the core logging subsystem.
/// Sets up a file sink in the specified directory and installs a custom Qt message handler.
void initLogger(const QString& logDirPath);

/// Shuts down the logging subsystem and cleans up the message handler.
void shutdownLogger();

} // namespace manga::core
