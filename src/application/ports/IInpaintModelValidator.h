#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::application {

struct InpaintModelStatus {
    bool isValid{false};
    QString id;
    QString version;
    QString modelDirectory;
    QString modelFile;
    QString inputName;
    QString outputName;
    QString minimumOnnxRuntime;
    QString executionProvider;

    friend bool operator==(const InpaintModelStatus&, const InpaintModelStatus&) = default;
};

class IInpaintModelValidator {
public:
    virtual ~IInpaintModelValidator() = default;
    [[nodiscard]] virtual Result<InpaintModelStatus> validate(const QString& modelDirectory) = 0;
};

} // namespace manga::application
