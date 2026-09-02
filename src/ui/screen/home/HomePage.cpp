#include "ui/screen/home/HomePage.h"

#include "ui/error/RecoveryPresenter.h"
#include "ui/screen/home/HomeState.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/home/RecentProjectCardDelegate.h"
#include "ui/screen/home/RecentProjectsModel.h"

#include <FluentQt/FluentQt.h>
#include <QBoxLayout>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QStackedLayout>
#include <QUrl>

namespace manga::ui::screen::home {

HomePage::HomePage(HomeViewModel* viewModel, QWidget* parent) : base::BasePage(parent), m_viewModel(viewModel) {
    buildUi();

    m_viewModel->observe(this, [this](const HomeState& state) { render(state); });

    connect(m_viewModel, &HomeViewModel::openProjectDialogRequested, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("打开 MangaTranslator 项目"));
        if (!dir.isEmpty()) {
            m_viewModel->requestOpenProjectPath(dir);
        }
    });

    connect(m_viewModel, &HomeViewModel::toastRequested, this, [this](const QString& message, bool isError) {
        fluent::status_info::Toast::showToast(
            this, message, isError ? fluent::status_info::Toast::Error : fluent::status_info::Toast::Success);
    });

    setDefaultFocusWidget(m_openButton);
}

void HomePage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 24, 32, 24);
    rootLayout->setSpacing(16);

    // Page header keeps the title block left and the commands right, per docs/ui/pages/home.md.
    auto* headerRow = new QHBoxLayout;
    auto* titleBlock = new QVBoxLayout;
    m_titleLabel = new QLabel(tr("欢迎回来"), this);
    m_titleLabel->setFont(themeFont(Typography::FontRole::Title).toQFont());
    titleBlock->addWidget(m_titleLabel);
    m_subtitleLabel = new QLabel(tr("继续漫画翻译项目，或开始新的工作。"), this);
    m_subtitleLabel->setFont(themeFont(Typography::FontRole::Body).toQFont());
    titleBlock->addWidget(m_subtitleLabel);
    headerRow->addLayout(titleBlock);
    headerRow->addStretch(1);

    m_openButton = new fluent::basicinput::Button(tr("打开项目"), this);
    m_openButton->setIconGlyph(Typography::Icons::Folder);
    m_openButton->setToolTip(tr("打开已有的 MangaTranslator 项目"));
    m_openButton->setAccessibleName(tr("打开项目"));
    connect(m_openButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->requestOpenProjectDialog();
    });
    headerRow->addWidget(m_openButton, 0, Qt::AlignVCenter);

    m_importButton = new fluent::basicinput::Button(tr("新建项目/导入"), this);
    m_importButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_importButton->setIconGlyph(Typography::Icons::Add);
    m_importButton->setToolTip(tr("从漫画图片或文件夹新建项目"));
    m_importButton->setAccessibleName(tr("新建项目/导入"));
    connect(m_importButton, &fluent::basicinput::Button::clicked, this, &HomePage::importRequested);
    headerRow->addWidget(m_importButton, 0, Qt::AlignVCenter);
    rootLayout->addLayout(headerRow);

    m_infoBar = new fluent::status_info::InfoBar(this);
    m_infoBar->setIsOpen(false);
    m_infoBar->setIsClosable(true);
    m_infoBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_infoBar->setCornerRadius(8);
    m_infoBar->setContentMargins(QMargins(14, 10, 14, 10));
    m_infoBar->setIconTextSpacing(12);
    m_infoBar->setTitleMessageSpacing(8);
    m_infoBar->setSeverityIconSize(20);
    connect(m_infoBar, &fluent::status_info::InfoBar::closed, this, [this]() { m_viewModel->dismissNotice(); });
    m_retryButton = new fluent::basicinput::Button(tr("重试"), this);
    m_retryButton->setToolTip(tr("重试上一次失败的操作"));
    m_retryButton->setAccessibleName(tr("重试"));
    m_retryButton->hide(); // Hosted by the InfoBar only while an actionable notice is shown.
    connect(m_retryButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (m_currentNoticeAction == application::RecoveryAction::Retry) {
            if (!m_currentRetryPath.isEmpty()) {
                m_viewModel->requestOpenProjectPath(m_currentRetryPath);
            }
        } else if (m_currentNoticeAction == application::RecoveryAction::ChoosePath) {
            m_viewModel->requestOpenProjectDialog();
        } else if (m_currentNoticeAction == application::RecoveryAction::OpenSettings ||
                   m_currentNoticeAction == application::RecoveryAction::Reauthenticate ||
                   m_currentNoticeAction == application::RecoveryAction::LocateModel) {
            Q_EMIT settingsRequested(ui::error::RecoveryPresenter::settingsRouteForAction(m_currentNoticeAction));
        } else if (m_currentNoticeAction == application::RecoveryAction::FreeDiskSpace) {
            if (!m_currentRetryPath.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentRetryPath));
            }
        }
    });
    rootLayout->addWidget(m_infoBar);

    // Recent projects section (full width).
    auto* recentHeader = new QLabel(tr("最近项目"), this);
    recentHeader->setFont(themeFont(Typography::FontRole::BodyStrong).toQFont());
    rootLayout->addWidget(recentHeader);

    auto* stackContainer = new QWidget(this);
    m_stack = new QStackedLayout(stackContainer);

    // Empty state shown instead of the recent projects grid, per docs/ui/pages/home.md.
    m_emptyState = new QWidget(stackContainer);
    m_emptyState->setAutoFillBackground(false);
    m_stack->addWidget(m_emptyState);

    // Recent projects card grid, homogeneous single-select cards per docs/ui/pages/home.md.
    m_gridView = new fluent::collections::GridView(stackContainer);
    m_model = new RecentProjectsModel(this);
    m_delegate = new RecentProjectCardDelegate(this, m_gridView);
    m_gridView->setModel(m_model);
    m_gridView->setItemDelegate(m_delegate);
    m_gridView->setCellSize(QSize(240, 216));
    m_gridView->setSelectionMode(fluent::collections::SelectionMode::Single);
    m_gridView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_gridView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_gridView->setHorizontalSpacing(16);
    m_gridView->setVerticalSpacing(16);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &HomePage::showContextMenu);
    const auto openAt = [this](const QModelIndex& index) {
        if (index.isValid()) {
            const QString path = index.data(RecentProjectsModel::PathRole).toString();
            if (!path.isEmpty()) {
                m_viewModel->requestOpenProjectPath(path);
            }
        }
    };
    connect(m_gridView, &QAbstractItemView::clicked, this, openAt);
    m_stack->addWidget(m_gridView);
    rootLayout->addWidget(stackContainer, 1);
}

void HomePage::render(const HomeState& state) {
    m_model->setProjects(state.recentProjects);
    m_stack->setCurrentWidget(state.recentProjects.isEmpty() ? m_emptyState : m_gridView);
    m_openButton->setEnabled(!state.isOpeningProject);

    if (state.notice.has_value()) {
        const auto& notice = *state.notice;
        m_infoBar->setSeverity(notice.severity == HomeNotice::Severity::Error
                                   ? fluent::status_info::InfoBar::Error
                                   : fluent::status_info::InfoBar::Informational);
        m_infoBar->setTitle(notice.severity == HomeNotice::Severity::Error ? tr("打开失败") : tr("提示"));
        m_infoBar->setMessage(notice.message);
        m_currentRetryPath = notice.retryPath;
        m_currentNoticeAction = notice.action;
        if (!notice.actionText.isEmpty()) {
            m_retryButton->setText(notice.actionText);
            m_retryButton->show();
            m_infoBar->setActionWidget(m_retryButton);
        } else {
            m_retryButton->hide();
            m_infoBar->setActionWidget(nullptr);
        }
        m_infoBar->setIsOpen(true);
    } else {
        m_currentRetryPath.clear();
        m_currentNoticeAction = application::RecoveryAction::None;
        m_infoBar->setIsOpen(false);
        m_infoBar->setActionWidget(nullptr);
    }
}

void HomePage::updateResponsiveLayout(int /*availableWidth*/) {}

void HomePage::showContextMenu(const QPoint& pos) {
    const QModelIndex index = m_gridView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString path = index.data(RecentProjectsModel::PathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    fluent::menus_toolbars::FluentMenu menu(QString(), this);
    auto* openAction = menu.addAction(tr("打开"));
    auto* showInFolderAction = menu.addAction(tr("在文件夹中显示"));
    auto* removeAction = menu.addAction(tr("从最近列表中移除"));
    const QAction* selected = menu.exec(m_gridView->viewport()->mapToGlobal(pos));
    if (selected == openAction) {
        m_viewModel->requestOpenProjectPath(path);
    } else if (selected == showInFolderAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (selected == removeAction) {
        m_viewModel->removeRecent(path);
    }
}

bool HomePage::isEmptyStateVisibleForTest() const {
    return m_stack != nullptr && m_stack->currentWidget() == m_emptyState;
}

fluent::basicinput::Button* HomePage::importButtonForTest() const {
    return m_importButton;
}

void HomePage::onThemeUpdated() {
    BasePage::onThemeUpdated();
    const auto& tokens = themeColorsRef();
    if (m_titleLabel) {
        m_titleLabel->setFont(themeFont(Typography::FontRole::Title).toQFont());
    }
    if (m_subtitleLabel) {
        m_subtitleLabel->setFont(themeFont(Typography::FontRole::Body).toQFont());
        QPalette palette = m_subtitleLabel->palette();
        palette.setColor(QPalette::WindowText, tokens.textSecondary);
        m_subtitleLabel->setPalette(palette);
    }
    if (m_gridView) {
        m_gridView->viewport()->update(); // delegate reads tokens at paint time
    }
}

} // namespace manga::ui::screen::home
