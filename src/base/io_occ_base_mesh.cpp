/****************************************************************************
** Copyright (c) 2020, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_occ_base_mesh.h"

#include "document.h"
#include "scope_import.h"
#include "occ_progress_indicator.h"
#include "task_progress.h"
#include "tkernel_utils.h"
#include <fougtools/occtools/qt_utils.h>

#include <RWMesh_CafReader.hxx>

namespace Mayo {
namespace IO {

OccBaseMeshReaderProperties::OccBaseMeshReaderProperties(PropertyGroup* parentGroup)
    : PropertyGroup(parentGroup),
      rootPrefix(this, textId("rootPrefix")),
      systemCoordinatesConverter(this, textId("systemCoordinatesConverter"), &enumCoordinateSystem()),
      systemLengthUnit(this, textId("systemLengthUnit"), &enumLengthUnit())
{
    this->rootPrefix.setDescription(tr("Prefix for generating root labels name"));
    this->systemLengthUnit.setDescription(tr("System length units to convert into while reading files"));
}

void OccBaseMeshReaderProperties::restoreDefaults()
{
    this->rootPrefix.setValue(QString());
    this->systemCoordinatesConverter.setValue(RWMesh_CoordinateSystem_Undefined);
    this->systemLengthUnit.setValue(int(LengthUnit::Undefined));
}

double OccBaseMeshReaderProperties::lengthUnitFactor(OccBaseMeshReaderProperties::LengthUnit lenUnit)
{
    switch (lenUnit) {
    case LengthUnit::Undefined: return -1;
    case LengthUnit::Micrometer: return 1e-6;
    case LengthUnit::Millimeter: return 0.001;
    case LengthUnit::Centimeter: return 0.01;
    case LengthUnit::Meter: return 1.;
    case LengthUnit::Kilometer: return 1000.;
    case LengthUnit::Inch: return 0.0254;
    case LengthUnit::Foot: return 0.3048;
    case LengthUnit::Mile: return 1609.344;
    }

    return -1;
}

OccBaseMeshReaderProperties::LengthUnit OccBaseMeshReaderProperties::lengthUnit(double factor)
{
    if (factor < 0)
        return LengthUnit::Undefined;

    for (const Enumeration::Item& enumItem : OccBaseMeshReaderProperties::enumLengthUnit().items()) {
        const auto lenUnit = static_cast<LengthUnit>(enumItem.value);
        const double lenUnitFactor = OccBaseMeshReaderProperties::lengthUnitFactor(lenUnit);
        if (factor == lenUnitFactor)
            return lenUnit;
    }

    return LengthUnit::Undefined;
}

const Enumeration& OccBaseMeshReaderProperties::enumLengthUnit()
{
    using LengthUnit = LengthUnit;
    static const Enumeration enumeration = {
        { int(LengthUnit::Undefined), textId("UnitUndefined") },
        { int(LengthUnit::Micrometer), textId("UnitMicrometer") },
        { int(LengthUnit::Millimeter), textId("UnitMillimeter") },
        { int(LengthUnit::Centimeter), textId("UnitCentimeter") },
        { int(LengthUnit::Meter), textId("UnitMeter") },
        { int(LengthUnit::Kilometer), textId("UnitKilometer") },
        { int(LengthUnit::Inch), textId("UnitInch") },
        { int(LengthUnit::Foot), textId("UnitFoot") },
        { int(LengthUnit::Mile), textId("UnitMile") }
    };
    return enumeration;
}

const Enumeration& OccBaseMeshReaderProperties::enumCoordinateSystem()
{
    static const Enumeration enumeration = {
        { RWMesh_CoordinateSystem_Undefined, textId("SystemUndefined") },
        { RWMesh_CoordinateSystem_Zup, textId("SystemPosZUp") },
        { RWMesh_CoordinateSystem_Yup, textId("SystemPosYUp") }
    };
    return enumeration;
}

bool OccBaseMeshReader::readFile(const QString& filepath, TaskProgress* progress)
{
    m_filepath = filepath;
    progress->setValue(100);
    return true;
}

bool OccBaseMeshReader::transfer(DocumentPtr doc, TaskProgress* progress)
{
    this->applyParameters();
    m_reader.SetDocument(doc);
    Handle_Message_ProgressIndicator indicator = new OccProgressIndicator(progress);
    XCafScopeImport import(doc);
    const bool okPerform = m_reader.Perform(
                occ::QtUtils::toOccUtf8String(m_filepath), TKernelUtils::start(indicator));
    import.setConfirmation(okPerform && !TaskProgress::isAbortRequested(progress));
    return okPerform;
}

void OccBaseMeshReader::applyProperties(const PropertyGroup* params)
{
    auto ptr = dynamic_cast<const OccBaseMeshReaderProperties*>(params);
    if (ptr) {
        this->parameters().systemCoordinatesConverter =
                ptr->systemCoordinatesConverter.valueAs<RWMesh_CoordinateSystem>();
        this->parameters().systemLengthUnit = ptr->systemLengthUnit.valueAs<LengthUnit>();
        this->parameters().rootPrefix = ptr->rootPrefix.value();
    }
}

OccBaseMeshReader::OccBaseMeshReader(RWMesh_CafReader& reader)
    : m_reader(reader)
{
}

void OccBaseMeshReader::applyParameters()
{
    m_reader.SetRootPrefix(occ::QtUtils::toOccUtf8String(this->constParameters().rootPrefix));
    m_reader.SetSystemLengthUnit(OccBaseMeshReaderProperties::lengthUnitFactor(this->constParameters().systemLengthUnit));
    m_reader.SetSystemCoordinateSystem(this->constParameters().systemCoordinatesConverter);
}

} // namespace IO
} // namespace Mayo
