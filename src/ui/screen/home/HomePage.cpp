#include "ui/screen/home/HomePage.h"

#include "ui/screen/home/HomeState.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/home/RecentProjectCardDelegate.h"
#include "ui/screen/home/RecentProjectsModel.h"

#include <FluentQt/FluentQt.h>
#include <QBoxLayout>
#include <QFileDialog>
#include <QLabel>
#include <QStackedLayout>

namespace manga::ui::screen::home {

namespace {
constexpr int kSideRailWidth = 320;      // spec §3.2: auxiliary column 300-340 px
constexpr int kCompactBreakpoint = 1008; // spec §3.3: below this the page flows single-column
} // namespace

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

    // Page header: title block left, commands right (spec §2.1).
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
    connect(m_openButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->requestOpenProjectDialog();
    });
    headerRow->addWidget(m_openButton, 0, Qt::AlignVCenter);

    m_importButton = new fluent::basicinput::Button(tr("新建项目/导入"), this);
    m_importButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_importButton->setIconGlyph(Typography::Icons::Add);
    connect(m_importButton, &fluent::basicinput::Button::clicked, this, &HomePage::importRequested);
    headerRow->addWidget(m_importButton, 0, Qt::AlignVCenter);
    rootLayout->addLayout(headerRow);

    m_infoBar = new fluent::status_info::InfoBar(this);
    m_infoBar->setIsOpen(false);
    m_infoBar->setIsClosable(true);
    connect(m_infoBar, &fluent::status_info::InfoBar::closed, this, [this]() { m_viewModel->dismissNotice(); });
    m_retryButton = new fluent::basicinput::Button(tr("重试"), this);
    m_retryButton->hide(); // Hosted by the InfoBar only while a retryable notice is shown.
    connect(m_retryButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (!m_currentRetryPath.isEmpty()) {
            m_viewModel->openProjectPath(m_currentRetryPath);
        }
    });
    rootLayout->addWidget(m_infoBar);

    // Main area: recent projects section.
    m_mainArea = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(m_mainArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);
    auto* recentHeader = new QLabel(tr("最近项目"), m_mainArea);
    recentHeader->setFont(themeFont(Typography::FontRole::BodyStrong).toQFont());
    mainLayout->addWidget(recentHeader);

    auto* stackContainer = new QWidget(m_mainArea);
    m_stack = new QStackedLayout(stackContainer);

    // Empty state (spec §5.1).
    m_emptyState = new QWidget(stackContainer);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch(1);
    auto* emptyGlyph = new QLabel(Typography::Icons::Document, m_emptyState);
    QFont emptyGlyphFont(Typography::FontFamily::FluentIcons);
    emptyGlyphFont.setPixelSize(48);
    emptyGlyph->setFont(emptyGlyphFont);
    emptyGlyph->setAlignment(Qt::AlignCenter);
    emptyGlyph->setAccessibleName(QString()); // decorative
    emptyLayout->addWidget(emptyGlyph);
    auto* emptyTitle = new QLabel(tr("创建第一个漫画项目"), m_emptyState);
    emptyTitle->setFont(themeFont(Typography::FontRole::Subtitle).toQFont());
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    auto* emptyHint = new QLabel(tr("导入图片或打开已有项目开始翻译。"), m_emptyState);
    emptyHint->setFont(themeFont(Typography::FontRole::Body).toQFont());
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyHint);
    auto* emptyButtons = new QHBoxLayout;
    emptyButtons->addStretch(1);
    auto* emptyNewButton = new fluent::basicinput::Button(tr("新建项目/导入"), m_emptyState);
    emptyNewButton->setFluentStyle(fluent::basicinput::Button::Accent);
    connect(emptyNewButton, &fluent::basicinput::Button::clicked, this, &HomePage::importRequested);
    emptyButtons->addWidget(emptyNewButton);
    auto* emptyOpenButton = new fluent::basicinput::Button(tr("打开已有项目"), m_emptyState);
    connect(emptyOpenButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->requestOpenProjectDialog();
    });
    emptyButtons->addWidget(emptyOpenButton);
    emptyButtons->addStretch(1);
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addStretch(1);
    m_stack->addWidget(m_emptyState);

    // Recent projects card grid (WinUI GridView semantics, spec §2.2).
    m_gridView = new fluent::collections::GridView(stackContainer);
    m_model = new RecentProjectsModel(this);
    m_delegate = new RecentProjectCardDelegate(this, m_gridView);
    m_gridView->setModel(m_model);
    m_gridView->setItemDelegate(m_delegate);
    m_gridView->setCellSize(QSize(240, 216));
    m_gridView->setHorizontalSpacing(16);
    m_gridView->setVerticalSpacing(16);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &HomePage::showContextMenu);
    const auto openAt = [this](const QModelIndex& index) {
        if (index.isValid()) {
            const QString path = index.data(RecentProjectsModel::PathRole).toString();
            if (!path.isEmpty()) {
                m_viewModel->openProjectPath(path);
            }
        }
    };
    connect(m_gridView, &QAbstractItemView::activated, this, openAt);
    connect(m_gridView, &QAbstractItemView::doubleClicked, this, openAt);
    m_stack->addWidget(m_gridView);
    mainLayout->addWidget(stackContainer, 1);

    // Side rail: quick start (spec §2.3). Import flows land in P5-B; buttons stay inert.
    m_sideRail = new QWidget(this);
    auto* railLayout = new QVBoxLayout(m_sideRail);
    railLayout->setContentsMargins(0, 0, 0, 0);
    railLayout->setSpacing(8);
    auto* quickHeader = new QLabel(tr("快速开始"), m_sideRail);
    quickHeader->setFont(themeFont(Typography::FontRole::BodyStrong).toQFont());
    railLayout->addWidget(quickHeader);
    const auto addQuickAction = [this, railLayout](const QString& glyph, const QString& text) {
        auto* button = new fluent::basicinput::Button(text, m_sideRail);
        button->setIconGlyph(glyph);
        button->setEnabled(false);
        button->setToolTip(tr("导入向导将在后续版本提供"));
        railLayout->addWidget(button);
        return button;
    };
    addQuickAction(Typography::Icons::Document, tr("从图片创建项目"));
    addQuickAction(Typography::Icons::Folder, tr("从图片文件夹创建项目"));
    railLayout->addStretch(1);

    m_contentLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_contentLayout->setSpacing(24);
    m_contentLayout->addWidget(m_mainArea, 1);
    m_contentLayout->addWidget(m_sideRail, 0);
    rootLayout->addLayout(m_contentLayout, 1);
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

void HomePage::updateResponsiveLayout(int availableWidth) {
    if (m_contentLayout == nullptr || m_sideRail == nullptr) {
        return;
    }
    const bool compact = availableWidth < kCompactBreakpoint;
    m_contentLayout->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_sideRail->setFixedWidth(compact ? QWIDGETSIZE_MAX : kSideRailWidth);
}

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
    auto* removeAction = menu.addAction(tr("从最近列表中移除"));
    const QAction* selected = menu.exec(m_gridView->viewport()->mapToGlobal(pos));
    if (selected == openAction) {
        m_viewModel->openProjectPath(path);
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
