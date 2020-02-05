/****************************************************************************
** Copyright (c) 2020, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "property.h"
#include <system_error>

#include "application.h"
#include "property_builtins.h"

namespace Mayo {

class Command : public PropertyOwnerSignals {
public:
    virtual std::error_code exec();

signals:
    void info(const QString& text);
    void error(const QString& text);
};

class CommandNewDocument : public Command {
public:
    std::error_code exec() override;
};

class CommandOpenDocuments : public Command {
    Q_OBJECT
public:
    PropertyQStringList propertyFilePaths;

    std::error_code exec() override;

signals:
    void opened(const QString& filePath);

private:
    void runImport(Document* doc, Application::PartFormat format, const QString& filePath);
};

} // namespace Mayo
