/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "application.h"

#include "document.h"
#include "document_item.h"
#include "xde_document_item.h"
#include "options.h"
#include "string_utils.h"

#include <fougtools/qttools/task/progress.h>

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include "io_iges.h"
#include "io_occ_brep.h"
#include "io_step.h"
#include "io_stl.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace Mayo {

namespace Internal {

template<size_t N>
bool matchToken(const char* buffer, const char (&token)[N])
{
    return std::strncmp(buffer, token, N - 1) == 0;
}

Application::PartFormat findPartFormatFromContents(
        const char *contentsBegin,
        size_t contentsBeginSize,
        uint64_t fullContentsSizeHint)
{
    // -- Binary STL ?
    static const size_t binaryStlHeaderSize = 80 + sizeof(uint32_t);
    if (contentsBeginSize >= binaryStlHeaderSize) {
        const uint32_t offset = 80; // Skip header
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(contentsBegin);
        const uint32_t facetsCount =
                bytes[offset]
                | (bytes[offset+1] << 8)
                | (bytes[offset+2] << 16)
                | (bytes[offset+3] << 24);
        const unsigned facetSize = (sizeof(float) * 12) + sizeof(uint16_t);
        if ((facetSize * facetsCount + binaryStlHeaderSize)
                == fullContentsSizeHint)
        {
            return Application::PartFormat::Stl;
        }
    }

    // -- IGES ?
    {
        // regex : ^.{72}S\s*[0-9]+\s*[\n\r\f]
        bool isIges = true;
        if (contentsBeginSize >= 80 && contentsBegin[72] == 'S') {
            for (int i = 73; i < 80 && isIges; ++i) {
                if (contentsBegin[i] != ' ' && !std::isdigit(contentsBegin[i]))
                    isIges = false;
            }
            if (isIges && (contentsBegin[80] == '\n'
                           || contentsBegin[80] == '\r'
                           || contentsBegin[80] == '\f'))
            {
                const int sVal = std::atoi(contentsBegin + 73);
                if (sVal == 1)
                    return Application::PartFormat::Iges;
            }
        }
    } // IGES

    contentsBegin = StringUtils::skipWhiteSpaces(contentsBegin, contentsBeginSize);

    // -- STEP ?
    {
        // regex : ^\s*ISO-10303-21\s*;\s*HEADER
        static const char stepIsoId[] = "ISO-10303-21";
        static const size_t stepIsoIdLen = sizeof(stepIsoId) - 1;
        static const char stepHeaderToken[] = "HEADER";
        static const size_t stepHeaderTokenLen = sizeof(stepHeaderToken) - 1;
        if (std::strncmp(contentsBegin, stepIsoId, stepIsoIdLen) == 0) {
            auto charIt = StringUtils::skipWhiteSpaces(
                        contentsBegin + stepIsoIdLen,
                        contentsBeginSize - stepIsoIdLen);
            if (*charIt == ';'
                    && (charIt - contentsBegin) < contentsBeginSize)
            {
                charIt = StringUtils::skipWhiteSpaces(
                            charIt + 1,
                            contentsBeginSize - (charIt - contentsBegin));
                if (std::strncmp(charIt, stepHeaderToken, stepHeaderTokenLen)
                        == 0)
                {
                    return Application::PartFormat::Step;
                }
            }
        }
    } // STEP

    // -- OpenCascade BREP ?
    {
        // regex : ^\s*DBRep_DrawableShape
        static const char occBRepToken[] = "DBRep_DrawableShape";
        if (matchToken(contentsBegin, occBRepToken))
            return Application::PartFormat::OccBrep;
    }

    // -- ASCII STL ?
    {
        // regex : ^\s*solid
        const char asciiStlToken[] = "solid";
        if (matchToken(contentsBegin, asciiStlToken))
            return Application::PartFormat::Stl;
    }

    // Fallback case
    return Application::PartFormat::Unknown;
}

} // namespace Internal

Application::Application(QObject *parent)
    : QObject(parent)
{
}

Application *Application::instance()
{
    static Application app;
    return &app;
}

int Application::documentCount() const
{
    return static_cast<int>(m_documents.size());
}

Document *Application::documentAt(int index) const
{
    const bool validIndex = 0 <= index && index < m_documents.size();
    return validIndex ? m_documents.at(index) : nullptr;
}

const std::vector<Document*>& Application::documents() const
{
    return m_documents;
}

void Application::addDocument(Document *doc)
{
    auto itFound = std::find(m_documents.cbegin(), m_documents.cend(), doc);
    if (doc != nullptr && itFound == m_documents.cend()) {
        QObject::connect(doc, &Document::propertyChanged, [=](Property* prop) {
            emit documentPropertyChanged(doc, prop);
        });
        QObject::connect(
                    doc, &Document::itemAdded,
                    this, &Application::documentItemAdded);
        QObject::connect(
                    doc, &Document::itemErased,
                    this, &Application::documentItemErased);
        QObject::connect(
                    doc, &Document::itemPropertyChanged,
                    this, &Application::documentItemPropertyChanged);
        m_documents.emplace_back(doc);
        emit documentAdded(doc);
    }
}

bool Application::eraseDocument(Document *doc)
{
    auto itFound = std::find(m_documents.cbegin(), m_documents.cend(), doc);
    if (itFound != m_documents.cend()) {
        m_documents.erase(itFound);
        doc->deleteLater();
        emit documentErased(doc);
        return true;
    }
    return false;
}

Application::ArrayDocumentConstIterator
Application::findDocumentByLocation(const QFileInfo& loc) const
{
    const QString locAbsoluteFilePath = loc.absoluteFilePath();
    auto itDocFound = std::find_if(
                m_documents.cbegin(),
                m_documents.cend(),
                [=](const Document* doc) {
        return QFileInfo(doc->filePath()).absoluteFilePath() == locAbsoluteFilePath;
    });
    return itDocFound;
}

IoBase::Result Application::importInDocument(
        Document* doc,
        PartFormat format,
        const QString &filepath,
        qttask::Progress* progress)
{
    progress->setStep(QFileInfo(filepath).fileName());
    switch (format) {
    case PartFormat::Iges: {
        IoIges io;
        return io.readFile(doc, filepath, progress);
    }
    case PartFormat::Step: {
        IoStep io;
        return io.readFile(doc, filepath, progress);
    }
    case PartFormat::OccBrep: {
        IoOccBRep io;
        return io.readFile(doc, filepath, progress);
    }
    case PartFormat::Stl: {
        IoStl_OpenCascade io;
        return io.readFile(doc, filepath, progress);
    }
    case PartFormat::Unknown:
        break;
    }
    return IoBase::Result::error(tr("Unknown error"));
}

IoBase::Result Application::exportDocumentItems(
        Span<const ApplicationItem> spanAppItem,
        PartFormat format,
        const ExportOptions &options,
        const QString &filepath,
        qttask::Progress *progress)
{
    progress->setStep(QFileInfo(filepath).fileName());
    switch (format) {
    case PartFormat::Iges: {
        IoIges io;
        return io.writeFile(spanAppItem, filepath, progress);
    }
    case PartFormat::Step: {
        IoStep io;
        return io.writeFile(spanAppItem, filepath, progress);
    }
    case PartFormat::OccBrep: {
        IoOccBRep io;
        return io.writeFile(spanAppItem, filepath, progress);
    }
    case PartFormat::Stl: {
        IoStl_OpenCascade io;
        return io.writeFile(spanAppItem, filepath, progress);
    }
    case PartFormat::Unknown:
        break;
    }
    return IoBase::Result::error(tr("Unknown error"));
}

bool Application::hasExportOptionsForFormat(Application::PartFormat format)
{
    return format == PartFormat::Stl;
}

const std::vector<Application::PartFormat>& Application::partFormats()
{
    const static std::vector<PartFormat> vecFormat = {
        PartFormat::Iges,
        PartFormat::Step,
        PartFormat::OccBrep,
        PartFormat::Stl
    };
    return vecFormat;
}

QString Application::partFormatFilter(Application::PartFormat format)
{
    switch (format) {
    case PartFormat::Iges: return tr("IGES files(*.iges *.igs)");
    case PartFormat::Step: return tr("STEP files(*.step *.stp)");
    case PartFormat::OccBrep: return tr("OpenCascade BREP files(*.brep *.occ)");
    case PartFormat::Stl: return tr("STL files(*.stl *.stla)");
    case PartFormat::Unknown: break;
    }
    return QString();
}

QStringList Application::partFormatFilters()
{
    QStringList filters;
    filters << Application::partFormatFilter(PartFormat::Iges)
            << Application::partFormatFilter(PartFormat::Step)
            << Application::partFormatFilter(PartFormat::OccBrep)
            << Application::partFormatFilter(PartFormat::Stl);
    return filters;
}

Application::PartFormat Application::findPartFormat(const QString &filepath)
{
    QFile file(filepath);
    if (file.open(QIODevice::ReadOnly)) {
#ifdef HAVE_GMIO
        gmio_stream qtstream = gmio_stream_qiodevice(&file);
        const gmio_stl_format stlFormat = gmio_stl_format_probe(&qtstream);
        if (stlFormat != GMIO_STL_FORMAT_UNKNOWN)
            return Application::PartFormat::Stl;
#endif
        std::array<char, 2048> contentsBegin;
        contentsBegin.fill(0);
        file.read(contentsBegin.data(), contentsBegin.size());
        return Internal::findPartFormatFromContents(
                    contentsBegin.data(), contentsBegin.size(), file.size());
    }
    return PartFormat::Unknown;
}

} // namespace Mayo
