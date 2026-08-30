#pragma once

#include "ui/base/BasePage.h"

class QLabel;
class QStackedLayout;

namespace fluent::basicinput {
class Button;
}
namespace fluent::collections {
class ListView;
}
namespace fluent::status_info {
class InfoBar;
}

namespace manga::ui::screen::home {

class HomeViewModel;
class RecentProjectsModel;
struct HomeState;

/// Home surface: welcome header, open action, recent projects list with empty state.
class HomePage : public base::BasePage {
    Q_OBJECT
public:
    explicit HomePage(HomeViewModel* viewModel, QWidget* parent = nullptr);

    /// Test-only observation hook.
    [[nodiscard]] bool isEmptyStateVisibleForTest() const;

    void onThemeUpdated() override;

private:
    void buildUi();
    void render(const HomeState& state);
    void showContextMenu(const QPoint& pos);

    HomeViewModel* m_viewModel; // non-owning
    RecentProjectsModel* m_model{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_subtitleLabel{nullptr};
    fluent::basicinput::Button* m_openButton{nullptr};
    fluent::status_info::InfoBar* m_infoBar{nullptr};
    fluent::basicinput::Button* m_retryButton{nullptr};
    QString m_currentRetryPath;
    QStackedLayout* m_stack{nullptr};
    QWidget* m_emptyState{nullptr};
    fluent::collections::ListView* m_listView{nullptr};
};

} // namespace manga::ui::screen::home
