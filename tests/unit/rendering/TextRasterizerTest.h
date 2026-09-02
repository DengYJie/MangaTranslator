#pragma once
#include <QObject>
class TextRasterizerTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void rendersHorizontalTextToImage();
    void rendersHorizontalTextAtResolvedPagePosition();
    void rendersVerticalTextToImage();
};
