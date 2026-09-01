#pragma once

#include <QString>

namespace manga::application {

struct InpaintModelFile {
    QString file;
};

struct InpaintModelInput {
    QString name;
    QString layout;
    QString color;
    float rangeMin{0.0f};
    float rangeMax{1.0f};
    bool dynamicSpatial{false};
};

struct InpaintModelOutput {
    QString name;
    QString layout;
    QString color;
};

struct InpaintTiling {
    int padMultiple{8};
    int tileCore{512};
    int tileContext{128};
};

} // namespace manga::application
