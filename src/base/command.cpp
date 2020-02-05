/****************************************************************************
** Copyright (c) 2020, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "command.h"

#include "application.h"
#include "document.h"
#include <QtCore/QDir>
#include <fougtools/qttools/task/manager.h>
#include <fougtools/qttools/task/runner_stdasync.h>

namespace Mayo {

std::error_code Command::exec()
{
    return {};
}

std::error_code CommandNewDocument::exec()
{
    static unsigned docSequenceId = 0;
    auto doc = new Document;
    doc->setLabel(tr("Anonymous%1").arg(++docSequenceId));
    Application::instance()->addDocument(doc);
}

std::error_code CommandOpenDocuments::exec()
{
    const QStringList& listFilePath = this->propertyFilePaths.value();
    if (listFilePath.isEmpty())
        return {};

    auto app = Application::instance();
    for (const QString& filePath : listFilePath) {
        const QFileInfo loc(filePath);
        const int docId = app->findDocumentByLocation(loc);
        if (docId != -1)
            continue; // Skip already open documents

        const QString locAbsoluteFilePath = QDir::toNativeSeparators(loc.absoluteFilePath());
        const Application::PartFormat fileFormat = Application::findPartFormat(locAbsoluteFilePath);
        if (fileFormat != Application::PartFormat::Unknown) {
            auto doc = new Document;
            doc->setLabel(loc.fileName());
            doc->setFilePath(locAbsoluteFilePath);
            app->addDocument(doc);
            this->runImport(doc, fileFormat, locAbsoluteFilePath);
        }
        else {
            emit this->error(tr("Unknown file format: '%1'").arg(filePath));
        }
    }
}

void CommandOpenDocuments::runImport(
        Document* doc, Application::PartFormat format, const QString& filePath)
{
    auto task = qttask::Manager::globalInstance()->newTask<qttask::StdAsync>();
    task->setTaskTitle(QFileInfo(filePath).fileName());
    task->run([=]{
        QTime chrono;
        chrono.start();
        const Application::IoResult result =
                Application::instance()->importInDocument(doc, format, filePath, &task->progress());
        if (result) {
            const QString msg =
                    tr("Imported file '%1'(time: %2ms)")
                    .arg(QFileInfo(filePath).fileName())
                    .arg(chrono.elapsed());
            this->info(msg);
            emit this->opened(filePath);
        }
        else {
            const QString msg =
                    tr("Failed to import file: '%1'\nError: %2")
                    .arg(filePath, result.errorText());
            this->error(msg);
        }
    });
}

} // namespace Mayo
