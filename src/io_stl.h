/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "io_handler.h"
#include "property_enumeration.h"
#include <QtCore/QCoreApplication>

namespace Mayo {

class IoStl_OpenCascade {
    Q_DECLARE_TR_FUNCTIONS(Mayo::IoStl_OpenCascade)
public:
    struct OptionsWrite : public PropertyOwner {
        OptionsWrite();
        PropertyEnumeration propertyStlFormat;
    };
    static OptionsWrite* optionsWrite();
    static PropertyOwner* optionsRead() { return nullptr; }

    static Io::Result readFile(
            Document* doc,
            const QString& filepath,
            qttask::Progress* progress);
    static Io::Result writeFile(
            Span<const ApplicationItem> spanAppItem,
            const QString& filepath,
            qttask::Progress* progress);

private:
    enum class StlFormat { Ascii, Binary };
    static const Enumeration& enum_StlFormat();
};

} // namespace Mayo

#if 0
#include "property_builtins.h"
#include "property_enumeration.h"
#include <gmio_core/text_format.h>
#include <gmio_stl/stl_format.h>
#include <QtCore/QCoreApplication>

namespace Mayo {

class Io_LibGmio : public IoBase {
    Q_DECLARE_TR_FUNCTIONS(Io_LibGmio)
public:
    Io_LibGmio() :
        m_propwrite_StlAsciiSolidName(&m_optionsWrite),
        m_propwrite_StlAsciiFloat32Precision(&m_optionsWrite)
    {
        this->setTag("LibGmio");
        static Enumeration float32Format;
        float32Format.map(GMIO_FLOAT_TEXT_FORMAT_DECIMAL_LOWERCASE, tr("Decimal"));
        float32Format.map(GMIO_FLOAT_TEXT_FORMAT_SCIENTIFIC_LOWERCASE, tr("Scientific lowercase"));
        float32Format.map(GMIO_FLOAT_TEXT_FORMAT_SCIENTIFIC_UPPERCASE, tr("Scientific uppercase"));
        float32Format.map(GMIO_FLOAT_TEXT_FORMAT_SHORTEST_LOWERCASE, tr("Shortest"));
    }

private:
    PropertyQByteArray m_propwrite_StlAsciiSolidName;
    //PropertyEnumeration m_propwrite_StlAsciiFloat32Format;
    PropertyUInt m_propwrite_StlAsciiFloat32Precision;
#endif

#ifdef HAVE_GMIO
    struct ExportOptions {
        gmio_stl_format stlFormat = GMIO_STL_FORMAT_UNKNOWN;
        std::string stlaSolidName;
        gmio_float_text_format stlaFloat32Format =
                GMIO_FLOAT_TEXT_FORMAT_SHORTEST_LOWERCASE;
        uint8_t stlaFloat32Precision = 9;
    };
#endif
