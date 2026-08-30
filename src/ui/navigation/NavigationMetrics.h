#pragma once

#include <FluentQt/Design.h>
#include <QVector>

namespace manga::ui::navigation {
class NavigationTreeWidget;
class NavigationSectionHeader;

/**
 * @brief Overflow entries (in layout order), used by the panel to render the
 * overflow flyout.
 *
 * Exactly one of node and header is non-null: node is a top-level node, header is a
 * section header.
 */
struct NavigationOverflowEntry {
    NavigationTreeWidget* node = nullptr;
    NavigationSectionHeader* header = nullptr;
};

/**
 * @brief Region a navigation item belongs to (MenuItems vs FooterMenuItems).
 */
enum class NavigationItemPosition {
    Top,   // Top main list area (MenuItems)
    Bottom // Bottom footer area (FooterMenuItems)
};

/**
 * @name Navigation layout metrics
 * @{
 */
// Matches the NavigationView navigation item standard row height (ControlHeight::Large).
constexpr int kItemHeight = 40;
constexpr int kTextLeftOffset = 40;
constexpr int kIconSlotWidth = Typography::IconSize::Standard;
// Breathing room between rows.
constexpr int kItemBgPaddingV = 2;
// Section header height: matches the navigation item row height.
constexpr int kSectionHeight = Spacing::ControlHeight::Large;
constexpr int kCompactPaneWidth = Breakpoints::NavigationPaneCompactWidth;
// Reserves clearance for the indicator bar and the outer border.
constexpr int kRowLeftInset = Spacing::XSmall;
// Keeps visual breathing room for the icon and the indicator.
constexpr int kContentStart = Spacing::Medium;
// Ensures the icon geometry stays centered.
constexpr int kIconAreaWidth = Typography::IconSize::Standard;
// Matches the WinUI3 ExpandCollapseChevron 40px click area.
constexpr int kChevronAreaWidth = 40;
constexpr int kChevronRightInset = 6;
// Extra layout width the category chevron occupies in Top mode.
constexpr int kTopChevronAddedWidth = 30;
// The compact chevron indicator uses the 12px native glyph.
constexpr int kChevronIconPixelSize = Typography::IconSize::Compact;
constexpr int kSelectionIndicatorWidth = 3;
// Visual height alignment.
constexpr int kSelectionIndicatorHeight = 16;
constexpr int kSelectionIndicatorTextGap = Spacing::Small;
// Matches the MenuFlyout row width.
constexpr int kCompactFlyoutRowWidth = 200;
constexpr int kCompactFlyoutAnchorOffsetX = Spacing::XSmall;
constexpr int kCompactFlyoutSlideOffset = 40;
// paintLayeredShadow expands 9px plus offsetY 2px; margin >= 12 avoids clipping the shadow.
constexpr int kShadowMargin = 12;
// Top mode horizontal padding.
constexpr int kTopBarItemHorizontalPadding = 12;
// Top mode item height: matches the 48px NavigationView top bar spec.
constexpr int kTopBarItemHeight = 48;
// Top mode horizontal indicator bar height.
constexpr int kTopSelectionIndicatorHeight = 3;
// Top mode indicator bar default/minimum width.
constexpr int kTopSelectionIndicatorWidth = 16;
// Top mode button spacing.
constexpr int kTopBarButtonSpacing = 4;
// Separator line thickness.
constexpr int kSeparatorLineThickness = 1;
// Separator leading margin (Left = top / Top = left): WinUI3 NavigationViewItemSeparatorMargin.
constexpr int kSeparatorLeadingMargin = 3;
// Separator trailing margin (Left = bottom / Top = right): WinUI3 NavigationViewItemSeparatorMargin.
constexpr int kSeparatorTrailingMargin = 4;
/** @} */
} // namespace manga::ui::navigation
