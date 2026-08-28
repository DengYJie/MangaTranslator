#pragma once

#include "domain/model/ComicPage.h"
#include "domain/model/ComicProject.h"

namespace manga::domain {

/// Builder specifically for Data layer rehydration.
/// Bypasses automatic ordering/dirty behaviors to strictly load persisted state.
class PageRehydrationBuilder {
public:
    explicit PageRehydrationBuilder(ComicPage& page) : m_page(page) {}

    /// Rehydration: inserts a region without re-assigning orderIndex.
    /// Caller must pre-validate ordering.
    bool loadRegion(TextRegion region) {
        return m_page.loadRegion(std::move(region));
    }

private:
    ComicPage& m_page;
};

/// Builder specifically for Data layer rehydration.
/// Bypasses automatic ordering/dirty behaviors to strictly load persisted state.
class ProjectRehydrationBuilder {
public:
    explicit ProjectRehydrationBuilder(ComicProject& project) : m_project(project) {}

    /// Rehydration: inserts a page without re-assigning orderIndex.
    /// Caller must pre-validate ordering.
    bool loadPage(ComicPage page) {
        return m_project.loadPage(std::move(page));
    }

private:
    ComicProject& m_project;
};

} // namespace manga::domain
