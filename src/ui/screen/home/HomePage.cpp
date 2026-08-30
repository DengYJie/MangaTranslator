#include "ui/screen/home/HomePage.h"

#include "ui/screen/home/HomeState.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/home/RecentProjectsModel.h"

#include <FluentQt/FluentQt.h>
#include <QFileDialog>
#include <QLabel>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace manga::ui::screen::home {

HomePage::HomePage(HomeViewModel* viewModel, QWidget* parent) : base::BasePage(parent), m_viewModel(viewModel) {
    buildUi();

    m_viewModel->observe(this, [this](const HomeState& state) { render(state); });

    connect(m_viewModel, &HomeViewModel::openProjectDialogRequested, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("打开 MangaTranslator 项目"));
        if (!dir.isEmpty()) {
            m_viewModel->openProjectPath(dir);
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

    m_titleLabel = new QLabel(tr("欢迎回来"), this);
    m_titleLabel->setFont(themeFont(Typography::FontRole::Title).toQFont());
    rootLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(tr("继续漫画翻译项目，或开始新的工作。"), this);
    m_subtitleLabel->setFont(themeFont(Typography::FontRole::Body).toQFont());
    rootLayout->addWidget(m_subtitleLabel);

    m_openButton = new fluent::basicinput::Button(tr("打开项目"), this);
    m_openButton->setIconGlyph(Typography::Icons::Folder);
    connect(m_openButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->requestOpenProjectDialog();
    });
    rootLayout->addWidget(m_openButton, 0, Qt::AlignLeft);

    m_infoBar = new fluent::status_info::InfoBar(this);
    m_infoBar->setIsOpen(false);
    m_infoBar->setIsClosable(true);
    connect(m_infoBar, &fluent::status_info::InfoBar::closed, this, [this]() { m_viewModel->dismissNotice(); });
    // Single reused retry action: InfoBar::setActionWidget detaches without deleting,
    // so allocating a fresh button per render would leak it.
    m_retryButton = new fluent::basicinput::Button(tr("重试"), this);
    connect(m_retryButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (!m_currentRetryPath.isEmpty()) {
            m_viewModel->openProjectPath(m_currentRetryPath);
        }
    });
    rootLayout->addWidget(m_infoBar);

    auto* contentContainer = new QWidget(this);
    m_stack = new QStackedLayout(contentContainer);

    m_emptyState = new QWidget(contentContainer);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    auto* emptyLabel = new QLabel(tr("还没有最近项目\n点击「打开项目」开始。"), m_emptyState);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setFont(themeFont(Typography::FontRole::BodyLarge).toQFont());
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addStretch();
    m_stack->addWidget(m_emptyState);

    m_listView = new fluent::collections::ListView(contentContainer);
    m_model = new RecentProjectsModel(this);
    m_listView->setModel(m_model);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &HomePage::showContextMenu);
    connect(m_listView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            const QString path = index.data(RecentProjectsModel::PathRole).toString();
            if (!path.isEmpty()) {
                m_viewModel->openProjectPath(path);
            }
        }
    });
    m_stack->addWidget(m_listView);

    rootLayout->addWidget(contentContainer, 1);
}

void HomePage::render(const HomeState& state) {
    m_model->setProjects(state.recentProjects);
    m_stack->setCurrentWidget(state.recentProjects.isEmpty() ? m_emptyState : m_listView);
    m_openButton->setEnabled(!state.isOpeningProject);

    if (state.notice.has_value()) {
        const auto& notice = *state.notice;
        m_infoBar->setSeverity(notice.severity == HomeNotice::Severity::Error
                                   ? fluent::status_info::InfoBar::Error
                                   : fluent::status_info::InfoBar::Informational);
        m_infoBar->setTitle(notice.severity == HomeNotice::Severity::Error ? tr("打开失败") : tr("提示"));
        m_infoBar->setMessage(notice.message);
        if (!notice.retryPath.isEmpty()) {
            m_currentRetryPath = notice.retryPath;
            m_infoBar->setActionWidget(m_retryButton);
        } else {
            m_currentRetryPath.clear();
            m_infoBar->setActionWidget(nullptr);
        }
        m_infoBar->setIsOpen(true);
    } else {
        m_infoBar->setIsOpen(false);
        m_infoBar->setActionWidget(nullptr);
    }
}

void HomePage::showContextMenu(const QPoint& pos) {
    const QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString path = index.data(RecentProjectsModel::PathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    fluent::menus_toolbars::FluentMenu menu(QString(), this);
    auto* openAction = menu.addAction(tr("打开"));
    auto* removeAction = menu.addAction(tr("从最近列表中移除"));
    const QAction* selected = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if (selected == openAction) {
        m_viewModel->openProjectPath(path);
    } else if (selected == removeAction) {
        m_viewModel->removeRecent(path);
    }
}

bool HomePage::isEmptyStateVisibleForTest() const {
    return m_stack != nullptr && m_stack->currentWidget() == m_emptyState;
}

void HomePage::onThemeUpdated() {
    BasePage::onThemeUpdated();
    if (m_titleLabel) {
        m_titleLabel->setFont(themeFont(Typography::FontRole::Title).toQFont());
    }
    if (m_subtitleLabel) {
        m_subtitleLabel->setFont(themeFont(Typography::FontRole::Body).toQFont());
    }
}

} // namespace manga::ui::screen::home
