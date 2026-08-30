#include "ui/base/BasePage.h"

#include <QLineEdit>
#include <QtTest>

namespace {

class ProbePage : public manga::ui::base::BasePage {
    Q_OBJECT
public:
    using BasePage::BasePage;
    QVector<int> responsiveWidths;
    int themeUpdates{0};
    void onThemeUpdated() override {
        manga::ui::base::BasePage::onThemeUpdated();
        ++themeUpdates;
    }

protected:
    void updateResponsiveLayout(int availableWidth) override {
        responsiveWidths.append(availableWidth);
    }
};

} // namespace

class BasePageTest : public QObject {
    Q_OBJECT
private slots:
    void resizeTriggersResponsiveHook() {
        ProbePage page;
        page.resize(800, 600);
        // Hidden widgets defer the resize event; show flushes the pending one.
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(page.responsiveWidths.contains(800), 2000);
        page.resize(500, 400);
        QTRY_VERIFY_WITH_TIMEOUT(page.responsiveWidths.contains(500), 2000);
    }

    void showFocusesDefaultWidget() {
        ProbePage page;
        auto* edit = new QLineEdit(&page);
        page.setDefaultFocusWidget(edit);
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(edit->hasFocus(), 2000);
    }

    void noDefaultFocusIsHarmless() {
        ProbePage page;
        page.show(); // no default focus set; must not crash
        QVERIFY(page.defaultFocusWidget() == nullptr);
    }
};

QTEST_MAIN(BasePageTest)
#include "base_page_test.moc"
