#pragma once

#include "application/dto/AppSettingsDto.h"
#include "application/error/ApplicationError.h"
#include "application/ports/IAppSettingsStore.h"

namespace manga::application {

/// Reads persisted executable settings, supplying defaults and clamping stored values into range.
class LoadAppSettingsUseCase {
public:
    explicit LoadAppSettingsUseCase(const IAppSettingsStore* store) : m_store(store) {}

    [[nodiscard]] AppSettingsSnapshot execute() const;

private:
    const IAppSettingsStore* m_store;
};

/// Validates a complete snapshot before writing namespaced keys. An invalid snapshot writes nothing.
/// Secrets never belong to the snapshot and are never persisted here.
class SaveAppSettingsUseCase {
public:
    explicit SaveAppSettingsUseCase(IAppSettingsStore* store) : m_store(store) {}

    [[nodiscard]] Result<void> execute(const AppSettingsSnapshot& snapshot);

private:
    IAppSettingsStore* m_store;
};

} // namespace manga::application
