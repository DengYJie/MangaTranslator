#include "app/ApplicationContext.h"

#include "application/workflow/OperationRegistry.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"

namespace manga::app {

struct ApplicationContext::ProductionAdapters {
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
};

ApplicationContext::ApplicationContext()
    : m_production(std::make_unique<ProductionAdapters>()),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()) {
    m_production->ioCoordinator = std::make_unique<data::ProjectIoCoordinator>();
    m_projectRepository = std::make_unique<data::FileSystemProjectRepository>(m_production->ioCoordinator.get());
    m_recentProjectsStore = std::make_unique<data::QSettingsRecentProjectsStore>();
    m_appSettingsStore = std::make_unique<data::QSettingsAppSettingsStore>();
}

ApplicationContext::ApplicationContext(std::unique_ptr<application::IProjectRepository> projectRepository,
                                       std::unique_ptr<application::IRecentProjectsStore> recentProjectsStore,
                                       std::unique_ptr<application::IAppSettingsStore> appSettingsStore)
    : m_projectRepository(std::move(projectRepository)), m_recentProjectsStore(std::move(recentProjectsStore)),
      m_appSettingsStore(std::move(appSettingsStore)),
      m_operationRegistry(std::make_unique<application::OperationRegistry>()) {}

ApplicationContext::~ApplicationContext() {
    shutdown();
}

void ApplicationContext::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    // Later phases cancel in-flight operations and close the open project session here
    // before adapters are destroyed. Destruction order below is the teardown order.
}

} // namespace manga::app
