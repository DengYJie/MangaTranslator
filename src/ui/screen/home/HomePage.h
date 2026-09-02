#pragma once

#include "application/error/ApplicationError.h"
#include "ui/base/BasePage.h"

class QBoxLayout;
class QLabel;
class QStackedLayout;

namespace fluent::basicinput {
class Button;
}
namespace fluent::collections {
class GridView;
}
namespace fluent::status_info {
class InfoBar;
}

namespace manga::ui::screen::home {

class HomeViewModel;
class RecentProjectCardDelegate;
class RecentProjectsModel;
struct HomeState;

/// Home surface: page header with commands, recent-projects card grid, quick start rail,
/// empty state, and InfoBar feedback. Layout follows docs/ui/pages/home.md.
class HomePage : public base::BasePage {
    Q_OBJECT
public:
    explicit HomePage(HomeViewModel* viewModel, QWidget* parent = nullptr);

    /// Test-only observation hook.
    [[nodiscard]] bool isEmptyStateVisibleForTest() const;
    [[nodiscard]] fluent::basicinput::Button* importButtonForTest() const;
    [[nodiscard]] fluent::collections::GridView* recentProjectsGridForTest() const {
        return m_gridView;
    }

    void onThemeUpdated() override;

Q_SIGNALS:
    void importRequested();
    void settingsRequested(const QString& category);

protected:
    void updateResponsiveLayout(int availableWidth) override;

private:
    void buildUi();
    void render(const HomeState& state);
    void showContextMenu(const QPoint& pos);

    HomeViewModel* m_viewModel; // non-owning
    RecentProjectsModel* m_model{nullptr};
    RecentProjectCardDelegate* m_delegate{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_subtitleLabel{nullptr};
    fluent::basicinput::Button* m_openButton{nullptr};
    fluent::basicinput::Button* m_importButton{nullptr};
    fluent::basicinput::Button* m_newProjectButton{nullptr};
    fluent::status_info::InfoBar* m_infoBar{nullptr};
    // Single reused retry action: InfoBar::setActionWidget detaches without deleting,
    // so allocating a fresh button per render would leak it.
    fluent::basicinput::Button* m_retryButton{nullptr};
    QString m_currentRetryPath;
    application::RecoveryAction m_currentNoticeAction{application::RecoveryAction::None};
    QStackedLayout* m_stack{nullptr};
    fluent::collections::GridView* m_gridView{nullptr};
};

} // namespace manga::ui::screen::home
