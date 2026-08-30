#pragma once
#include <QObject>
class TextRasterizerTest : public QObject {
    Q_OBJECT
private slots:
    void rendersHorizontalTextToImage();
    void rendersHorizontalTextAtResolvedPagePosition();
    void rendersVerticalTextToImage();
};
