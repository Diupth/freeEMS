#include "OemProject.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rfsim {
namespace {

QString number(double value) {
    return QString::number(value, 'g', 17);
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

[[noreturn]] void invalid(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString requiredAttribute(const QXmlStreamAttributes& attributes,
                          const QString& name) {
    const auto value = attributes.value(name);
    if (value.isNull() || value.isEmpty()) {
        invalid(QStringLiteral("Missing project attribute: %1").arg(name));
    }
    return value.toString();
}

double numberAttribute(const QXmlStreamAttributes& attributes,
                       const QString& name) {
    bool ok = false;
    const double value = requiredAttribute(attributes, name).toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        invalid(QStringLiteral("Invalid numeric project attribute: %1").arg(name));
    }
    return value;
}

int integerAttribute(const QXmlStreamAttributes& attributes,
                     const QString& name) {
    bool ok = false;
    const int value = requiredAttribute(attributes, name).toInt(&ok);
    if (!ok) {
        invalid(QStringLiteral("Invalid integer project attribute: %1").arg(name));
    }
    return value;
}

void writeNumber(QXmlStreamWriter& writer, const QString& name, double value) {
    writer.writeAttribute(name, number(value));
}

QString safeRelativePath(const QString& path) {
    const QString normalized = QDir::fromNativeSeparators(path);
    const QString clean = QDir::cleanPath(normalized);
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized) ||
        clean == QStringLiteral(".") || clean == QStringLiteral("..") ||
        clean.startsWith(QStringLiteral("../")) ||
        clean.contains(QStringLiteral("/../"))) {
        invalid(QStringLiteral("Invalid project result path: %1").arg(path));
    }
    return clean;
}

bool isRawFieldDump(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("h5") ||
           suffix == QStringLiteral("hdf5") ||
           suffix == QStringLiteral("vtk");
}

void runPackageTool(const QStringList& arguments) {
    QString python = qEnvironmentVariable("OPENEMS_PYTHON");
#ifdef RF_SIM_PYTHON_DEFAULT
    if (python.isEmpty()) python = QString::fromUtf8(RF_SIM_PYTHON_DEFAULT);
#endif
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    }
#ifdef RF_SIM_OEM_PACKAGE_SCRIPT
    const QString script = QString::fromUtf8(RF_SIM_OEM_PACKAGE_SCRIPT);
#else
    const QString script;
#endif
    if (python.isEmpty() || script.isEmpty()) {
        invalid(QStringLiteral("The Python interpreter or OEM package helper is unavailable."));
    }
    QProcess process;
    process.start(python, QStringList{script} + arguments);
    if (!process.waitForStarted() || !process.waitForFinished(-1) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        invalid(details.isEmpty()
                    ? QStringLiteral("OEM package helper failed: %1")
                          .arg(process.errorString())
                    : details);
    }
}

void writeAsset(const QString& root, const QString& relativePath,
                const QByteArray& data) {
    const QString safePath = safeRelativePath(relativePath);
    const QString destination = QDir(root).filePath(safePath);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        invalid(QStringLiteral("Cannot create package asset directory: %1")
                    .arg(safePath));
    }
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(data) != data.size() || file.error() != QFileDevice::NoError) {
        invalid(QStringLiteral("Cannot write package asset %1: %2")
                    .arg(safePath, file.errorString()));
    }
}

void writeSolid(QXmlStreamWriter& writer, const SolidObject& object, size_t index,
                QTemporaryDir& geometryDirectory) {
    writer.writeStartElement(QStringLiteral("solid"));
    writer.writeAttribute(QStringLiteral("id"), QString::fromStdString(object.id));
    writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(object.name));
    writer.writeAttribute(QStringLiteral("material-ref"),
                          QString::fromStdString(object.materialId));
    const QString kind = object.kind == SolidKind::Box
                             ? QStringLiteral("box")
                             : object.kind == SolidKind::Cylinder
                                   ? QStringLiteral("cylinder")
                                   : object.kind == SolidKind::Curve
                                       ? QStringLiteral("curve")
                                       : object.kind == SolidKind::Surface
                                             ? QStringLiteral("surface")
                                   : object.kind == SolidKind::Imported
                                         ? QStringLiteral("imported")
                                         : QStringLiteral("plate-with-hole");
    writer.writeAttribute(QStringLiteral("kind"), kind);
    writeNumber(writer, QStringLiteral("x-mm"), object.x);
    writeNumber(writer, QStringLiteral("y-mm"), object.y);
    writeNumber(writer, QStringLiteral("z-mm"), object.z);
    writeNumber(writer, QStringLiteral("rotation-x-deg"), object.rotationXDegrees);
    writeNumber(writer, QStringLiteral("rotation-y-deg"), object.rotationYDegrees);
    writeNumber(writer, QStringLiteral("rotation-z-deg"), object.rotationZDegrees);
    writeNumber(writer, QStringLiteral("length-mm"), object.length);
    writeNumber(writer, QStringLiteral("width-mm"), object.width);
    writeNumber(writer, QStringLiteral("height-mm"), object.height);
    writeNumber(writer, QStringLiteral("radius-mm"), object.radius);
    writeNumber(writer, QStringLiteral("hole-radius-mm"), object.holeRadius);
    if (object.kind == SolidKind::Curve) {
        writer.writeStartElement(QStringLiteral("curve-points"));
        for (const auto& point : object.curvePoints) {
            writer.writeStartElement(QStringLiteral("point"));
            writeNumber(writer, QStringLiteral("x-mm"), point.x);
            writeNumber(writer, QStringLiteral("y-mm"), point.y);
            writeNumber(writer, QStringLiteral("z-mm"), point.z);
            writer.writeEndElement();
        }
        writer.writeEndElement();
    } else if (object.kind == SolidKind::Imported ||
               object.kind == SolidKind::Surface) {
        if (object.customShape.IsNull()) {
            invalid(QStringLiteral("Imported solid %1 has no geometry.")
                        .arg(QString::fromStdString(object.name)));
        }
        const QString relativePath =
            QStringLiteral("geometry/solid_%1.step").arg(index);
        const QString stepPath = geometryDirectory.filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(stepPath).absolutePath())) {
            invalid(QStringLiteral("Cannot create project geometry directory."));
        }
        try {
            writeStep(object.customShape, stepPath.toStdString());
        } catch (const std::exception& exception) {
            invalid(QString::fromUtf8(exception.what()));
        }
        writer.writeStartElement(QStringLiteral("step-geometry"));
        writer.writeAttribute(QStringLiteral("path"), relativePath);
        writer.writeEndElement();
    }
    writer.writeEndElement();
}

void writeSimulation(QXmlStreamWriter& writer,
                     const OemSimulationSettings& settings) {
    writer.writeStartElement(QStringLiteral("simulation"));
    writeNumber(writer, QStringLiteral("frequency-start-ghz"),
                settings.frequencyStartGHz);
    writeNumber(writer, QStringLiteral("frequency-stop-ghz"),
                settings.frequencyStopGHz);
    writer.writeAttribute(QStringLiteral("frequency-points"),
                          QString::number(settings.frequencyPoints));
    writer.writeAttribute(QStringLiteral("mesh-divisions"),
                          QString::number(settings.meshDivisions));
    writer.writeAttribute(QStringLiteral("maximum-timesteps"),
                          QString::number(settings.maximumTimesteps));
    writer.writeAttribute(QStringLiteral("boundary"), settings.boundary);
    writer.writeAttribute(QStringLiteral("port-axis"), settings.portAxis);
    writeNumber(writer, QStringLiteral("port-start-x-mm"), settings.portStartXmm);
    writeNumber(writer, QStringLiteral("port-start-y-mm"), settings.portStartYmm);
    writeNumber(writer, QStringLiteral("port-start-z-mm"), settings.portStartZmm);
    writeNumber(writer, QStringLiteral("port-stop-x-mm"), settings.portStopXmm);
    writeNumber(writer, QStringLiteral("port-stop-y-mm"), settings.portStopYmm);
    writeNumber(writer, QStringLiteral("port-stop-z-mm"), settings.portStopZmm);
    writeNumber(writer, QStringLiteral("port-impedance-ohm"),
                settings.portImpedanceOhm);
    writer.writeAttribute(QStringLiteral("engine"), settings.engine);
    writer.writeEndElement();
}

void writeDesign(QXmlStreamWriter& writer, const OemProjectDocument& project,
                 QTemporaryDir& packageDirectory) {
    writer.writeStartElement(QStringLiteral("design"));
    writer.writeStartElement(QStringLiteral("parameters"));
    for (const auto& parameter : project.parameters) {
        writer.writeStartElement(QStringLiteral("parameter"));
        writer.writeAttribute(QStringLiteral("id"), parameter.id);
        writer.writeAttribute(QStringLiteral("name"), parameter.name);
        writer.writeAttribute(QStringLiteral("unit"), parameter.unit);
        writer.writeAttribute(QStringLiteral("expression"), parameter.expression);
        writer.writeAttribute(QStringLiteral("evaluated-value"),
                              number(parameter.evaluatedValue));
        writer.writeAttribute(QStringLiteral("role"), parameter.role);
        writer.writeAttribute(QStringLiteral("optimizable"),
                              parameter.optimizable ? QStringLiteral("true")
                                                    : QStringLiteral("false"));
        if (parameter.optimizable) {
            writeNumber(writer, QStringLiteral("minimum"), parameter.minimum);
            writeNumber(writer, QStringLiteral("maximum"), parameter.maximum);
            writeNumber(writer, QStringLiteral("step"), parameter.step);
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("materials"));
    for (const auto& material : project.materials) {
        writer.writeStartElement(QStringLiteral("material"));
        writer.writeAttribute(QStringLiteral("id"), material.id);
        writer.writeAttribute(QStringLiteral("name"), material.name);
        writer.writeAttribute(QStringLiteral("type"), material.type);
        writeNumber(writer, QStringLiteral("relative-permittivity"),
                    material.relativePermittivity);
        writeNumber(writer, QStringLiteral("conductivity-s-per-m"),
                    material.conductivitySPerM);
        writeNumber(writer, QStringLiteral("loss-tangent"), material.lossTangent);
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("feature-history"));
    for (const auto& feature : project.featureHistory) {
        writer.writeStartElement(QStringLiteral("feature"));
        writer.writeAttribute(QStringLiteral("id"), feature.id);
        writer.writeAttribute(QStringLiteral("type"), feature.type);
        writer.writeAttribute(QStringLiteral("output-id"), feature.outputId);
        writer.writeAttribute(QStringLiteral("output-kind"), feature.outputKind);
        if (!feature.materialRef.isEmpty()) {
            writer.writeAttribute(QStringLiteral("material-ref"),
                                  feature.materialRef);
        }
        for (const QString& input : feature.inputs) {
            writer.writeStartElement(QStringLiteral("input"));
            writer.writeAttribute(QStringLiteral("feature-ref"), input);
            writer.writeEndElement();
        }
        for (const auto& argument : feature.arguments) {
            writer.writeStartElement(QStringLiteral("argument"));
            writer.writeAttribute(QStringLiteral("name"), argument.name);
            writer.writeAttribute(QStringLiteral("expression"),
                                  argument.expression);
            writer.writeAttribute(QStringLiteral("unit"), argument.unit);
            writeNumber(writer, QStringLiteral("evaluated-value"),
                        argument.evaluatedValue);
            if (!argument.parameterRef.isEmpty()) {
                writer.writeAttribute(QStringLiteral("parameter-ref"),
                                      argument.parameterRef);
            }
            writer.writeEndElement();
        }
        if (!feature.geometryCache.IsNull()) {
            const QString cachePath =
                QStringLiteral("history/%1.step").arg(feature.id);
            const QString fullPath = packageDirectory.filePath(cachePath);
            if (!QDir().mkpath(QFileInfo(fullPath).absolutePath())) {
                invalid(QStringLiteral("Cannot create feature geometry cache directory."));
            }
            try {
                writeStep(feature.geometryCache, fullPath.toStdString());
            } catch (const std::exception& exception) {
                invalid(QString::fromUtf8(exception.what()));
            }
            writer.writeStartElement(QStringLiteral("geometry-cache"));
            writer.writeAttribute(QStringLiteral("path"), cachePath);
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
}

void parseSolid(QXmlStreamReader& reader, SolidObject* object,
                QTemporaryDir& geometryDirectory) {
    const auto attributes = reader.attributes();
    object->id = requiredAttribute(attributes, QStringLiteral("id")).toStdString();
    object->materialId =
        requiredAttribute(attributes, QStringLiteral("material-ref")).toStdString();
    object->name = requiredAttribute(attributes, QStringLiteral("name")).toStdString();
    const QString kind = requiredAttribute(attributes, QStringLiteral("kind"));
    if (kind == QStringLiteral("box")) {
        object->kind = SolidKind::Box;
    } else if (kind == QStringLiteral("cylinder")) {
        object->kind = SolidKind::Cylinder;
    } else if (kind == QStringLiteral("imported")) {
        object->kind = SolidKind::Imported;
    } else if (kind == QStringLiteral("curve")) {
        object->kind = SolidKind::Curve;
    } else if (kind == QStringLiteral("surface")) {
        object->kind = SolidKind::Surface;
    } else if (kind == QStringLiteral("plate-with-hole")) {
        object->kind = SolidKind::PlateWithHole;
    } else {
        invalid(QStringLiteral("Unknown project solid kind: %1").arg(kind));
    }

    object->x = numberAttribute(attributes, QStringLiteral("x-mm"));
    object->y = numberAttribute(attributes, QStringLiteral("y-mm"));
    object->z = numberAttribute(attributes, QStringLiteral("z-mm"));
    object->rotationXDegrees =
        numberAttribute(attributes, QStringLiteral("rotation-x-deg"));
    object->rotationYDegrees =
        numberAttribute(attributes, QStringLiteral("rotation-y-deg"));
    object->rotationZDegrees =
        numberAttribute(attributes, QStringLiteral("rotation-z-deg"));
    object->length = numberAttribute(attributes, QStringLiteral("length-mm"));
    object->width = numberAttribute(attributes, QStringLiteral("width-mm"));
    object->height = numberAttribute(attributes, QStringLiteral("height-mm"));
    object->radius = numberAttribute(attributes, QStringLiteral("radius-mm"));
    object->holeRadius =
        numberAttribute(attributes, QStringLiteral("hole-radius-mm"));

    QString stepRelativePath;
    while (reader.readNextStartElement()) {
        if (reader.name() == QStringLiteral("step-geometry")) {
            if (!stepRelativePath.isEmpty()) {
                invalid(QStringLiteral("Project solid contains duplicate STEP geometry."));
            }
            stepRelativePath = safeRelativePath(requiredAttribute(
                reader.attributes(), QStringLiteral("path")));
            reader.skipCurrentElement();
        } else if (reader.name() == QStringLiteral("curve-points")) {
            while (reader.readNextStartElement()) {
                if (reader.name() != QStringLiteral("point")) {
                    invalid(QStringLiteral("Unknown curve point element."));
                }
                const auto pointAttributes = reader.attributes();
                object->curvePoints.push_back({
                    numberAttribute(pointAttributes, QStringLiteral("x-mm")),
                    numberAttribute(pointAttributes, QStringLiteral("y-mm")),
                    numberAttribute(pointAttributes, QStringLiteral("z-mm"))
                });
                reader.skipCurrentElement();
            }
        } else {
            invalid(QStringLiteral("Unknown project solid element: %1")
                        .arg(reader.name().toString()));
        }
    }
    if (object->kind == SolidKind::Imported ||
        object->kind == SolidKind::Surface) {
        if (stepRelativePath.isEmpty() ||
            !stepRelativePath.startsWith(QStringLiteral("geometry/")) ||
            !stepRelativePath.endsWith(QStringLiteral(".step"),
                                       Qt::CaseInsensitive)) {
            invalid(QStringLiteral("Imported project geometry is missing its STEP asset."));
        }
        const QString stepPath = geometryDirectory.filePath(stepRelativePath);
        if (!QFileInfo::exists(stepPath)) {
            invalid(QStringLiteral("Imported STEP asset does not exist: %1")
                        .arg(stepRelativePath));
        }
        object->customShape = readStep(stepPath.toStdString());
    } else if (object->kind == SolidKind::Curve) {
        if (!stepRelativePath.isEmpty() || object->curvePoints.size() < 2) {
            invalid(QStringLiteral("A curve requires at least two stored control points."));
        }
        object->customShape = makePolylineCurve(object->curvePoints);
    } else if (!stepRelativePath.isEmpty()) {
        invalid(QStringLiteral("STEP geometry is only valid for imported shapes."));
    }
}

void parseGeometry(QXmlStreamReader& reader, OemProjectDocument* project,
                   QTemporaryDir& geometryDirectory) {
    while (reader.readNextStartElement()) {
        if (reader.name() != QStringLiteral("solid")) {
            invalid(QStringLiteral("Unknown project geometry element: %1")
                        .arg(reader.name().toString()));
        }
        if (project->solids.size() >= 10000) {
            invalid(QStringLiteral("Project contains too many solids."));
        }
        SolidObject object;
        parseSolid(reader, &object, geometryDirectory);
        project->solids.push_back(std::move(object));
    }
}

void parseSimulation(QXmlStreamReader& reader,
                     OemSimulationSettings* settings) {
    const auto attributes = reader.attributes();
    settings->frequencyStartGHz =
        numberAttribute(attributes, QStringLiteral("frequency-start-ghz"));
    settings->frequencyStopGHz =
        numberAttribute(attributes, QStringLiteral("frequency-stop-ghz"));
    settings->frequencyPoints =
        integerAttribute(attributes, QStringLiteral("frequency-points"));
    settings->meshDivisions =
        integerAttribute(attributes, QStringLiteral("mesh-divisions"));
    settings->maximumTimesteps =
        integerAttribute(attributes, QStringLiteral("maximum-timesteps"));
    settings->boundary = requiredAttribute(attributes, QStringLiteral("boundary"));
    settings->portAxis = requiredAttribute(attributes, QStringLiteral("port-axis"));
    settings->portStartXmm =
        numberAttribute(attributes, QStringLiteral("port-start-x-mm"));
    settings->portStartYmm =
        numberAttribute(attributes, QStringLiteral("port-start-y-mm"));
    settings->portStartZmm =
        numberAttribute(attributes, QStringLiteral("port-start-z-mm"));
    settings->portStopXmm =
        numberAttribute(attributes, QStringLiteral("port-stop-x-mm"));
    settings->portStopYmm =
        numberAttribute(attributes, QStringLiteral("port-stop-y-mm"));
    settings->portStopZmm =
        numberAttribute(attributes, QStringLiteral("port-stop-z-mm"));
    settings->portImpedanceOhm =
        numberAttribute(attributes, QStringLiteral("port-impedance-ohm"));
    settings->engine = requiredAttribute(attributes, QStringLiteral("engine"));
    reader.skipCurrentElement();
}

void parseDesign(QXmlStreamReader& reader, OemProjectDocument* project,
                 const QString& packageRoot) {
    bool hasParameters = false;
    bool hasMaterials = false;
    bool hasHistory = false;
    while (reader.readNextStartElement()) {
        if (reader.name() == QStringLiteral("parameters") && !hasParameters) {
            hasParameters = true;
            while (reader.readNextStartElement()) {
                if (reader.name() != QStringLiteral("parameter")) {
                    invalid(QStringLiteral("Unknown design parameter element: %1")
                                .arg(reader.name().toString()));
                }
                if (project->parameters.size() >= 100000) {
                    invalid(QStringLiteral("Project contains too many parameters."));
                }
                const auto attributes = reader.attributes();
                OemParameter parameter;
                parameter.id = requiredAttribute(attributes, QStringLiteral("id"));
                parameter.name = requiredAttribute(attributes, QStringLiteral("name"));
                parameter.unit = requiredAttribute(attributes, QStringLiteral("unit"));
                parameter.expression =
                    requiredAttribute(attributes, QStringLiteral("expression"));
                parameter.role = requiredAttribute(attributes, QStringLiteral("role"));
                parameter.evaluatedValue =
                    numberAttribute(attributes, QStringLiteral("evaluated-value"));
                const QString optimizable =
                    requiredAttribute(attributes, QStringLiteral("optimizable"));
                if (optimizable == QStringLiteral("true")) {
                    parameter.optimizable = true;
                    parameter.minimum =
                        numberAttribute(attributes, QStringLiteral("minimum"));
                    parameter.maximum =
                        numberAttribute(attributes, QStringLiteral("maximum"));
                    parameter.step =
                        numberAttribute(attributes, QStringLiteral("step"));
                } else if (optimizable != QStringLiteral("false")) {
                    invalid(QStringLiteral("Invalid optimizable value for parameter %1")
                                .arg(parameter.id));
                }
                reader.skipCurrentElement();
                project->parameters.push_back(std::move(parameter));
            }
        } else if (reader.name() == QStringLiteral("materials") && !hasMaterials) {
            hasMaterials = true;
            while (reader.readNextStartElement()) {
                if (reader.name() != QStringLiteral("material")) {
                    invalid(QStringLiteral("Unknown material element: %1")
                                .arg(reader.name().toString()));
                }
                if (project->materials.size() >= 10000) {
                    invalid(QStringLiteral("Project contains too many materials."));
                }
                const auto attributes = reader.attributes();
                OemMaterial material;
                material.id = requiredAttribute(attributes, QStringLiteral("id"));
                material.name = requiredAttribute(attributes, QStringLiteral("name"));
                material.type = requiredAttribute(attributes, QStringLiteral("type"));
                material.relativePermittivity =
                    numberAttribute(attributes, QStringLiteral("relative-permittivity"));
                material.conductivitySPerM =
                    numberAttribute(attributes, QStringLiteral("conductivity-s-per-m"));
                material.lossTangent =
                    numberAttribute(attributes, QStringLiteral("loss-tangent"));
                reader.skipCurrentElement();
                project->materials.push_back(std::move(material));
            }
        } else if (reader.name() == QStringLiteral("feature-history") && !hasHistory) {
            hasHistory = true;
            while (reader.readNextStartElement()) {
                if (reader.name() != QStringLiteral("feature")) {
                    invalid(QStringLiteral("Unknown feature history element: %1")
                                .arg(reader.name().toString()));
                }
                if (project->featureHistory.size() >= 100000) {
                    invalid(QStringLiteral("Project contains too many features."));
                }
                const auto attributes = reader.attributes();
                OemFeature feature;
                feature.id = requiredAttribute(attributes, QStringLiteral("id"));
                feature.type = requiredAttribute(attributes, QStringLiteral("type"));
                feature.outputId =
                    requiredAttribute(attributes, QStringLiteral("output-id"));
                feature.outputKind =
                    requiredAttribute(attributes, QStringLiteral("output-kind"));
                feature.materialRef =
                    attributes.value(QStringLiteral("material-ref")).toString();
                while (reader.readNextStartElement()) {
                    if (reader.name() == QStringLiteral("input")) {
                        feature.inputs.push_back(requiredAttribute(
                            reader.attributes(), QStringLiteral("feature-ref")));
                        reader.skipCurrentElement();
                    } else if (reader.name() == QStringLiteral("argument")) {
                        const auto argumentAttributes = reader.attributes();
                        OemFeatureArgument argument;
                        argument.name = requiredAttribute(
                            argumentAttributes, QStringLiteral("name"));
                        argument.expression = requiredAttribute(
                            argumentAttributes, QStringLiteral("expression"));
                        argument.unit = requiredAttribute(
                            argumentAttributes, QStringLiteral("unit"));
                        argument.evaluatedValue = numberAttribute(
                            argumentAttributes, QStringLiteral("evaluated-value"));
                        argument.parameterRef = argumentAttributes.value(
                            QStringLiteral("parameter-ref")).toString();
                        reader.skipCurrentElement();
                        feature.arguments.push_back(std::move(argument));
                    } else if (reader.name() == QStringLiteral("geometry-cache")) {
                        const QString path = safeRelativePath(requiredAttribute(
                            reader.attributes(), QStringLiteral("path")));
                        if (!path.startsWith(QStringLiteral("history/")) ||
                            !path.endsWith(QStringLiteral(".step"),
                                           Qt::CaseInsensitive) ||
                            !feature.geometryCache.IsNull()) {
                            invalid(QStringLiteral("Invalid feature geometry cache: %1")
                                        .arg(path));
                        }
                        const QString fullPath = QDir(packageRoot).filePath(path);
                        if (!QFileInfo::exists(fullPath)) {
                            invalid(QStringLiteral("Feature geometry cache is missing: %1")
                                        .arg(path));
                        }
                        feature.geometryCache = readStep(fullPath.toStdString());
                        reader.skipCurrentElement();
                    } else {
                        invalid(QStringLiteral("Unknown feature child element: %1")
                                    .arg(reader.name().toString()));
                    }
                }
                project->featureHistory.push_back(std::move(feature));
            }
        } else {
            invalid(QStringLiteral("Unknown or duplicate design section: %1")
                        .arg(reader.name().toString()));
        }
    }
    if (!hasParameters || !hasMaterials || !hasHistory) {
        invalid(QStringLiteral("Design section is missing a required subsection."));
    }
}

void applyEvaluatedParameters(OemProjectDocument* project) {
    QMap<QString, double> values;
    for (const auto& parameter : project->parameters) {
        values.insert(parameter.id, parameter.evaluatedValue);
    }
    for (auto& feature : project->featureHistory) {
        auto solid = std::find_if(
            project->solids.begin(), project->solids.end(),
            [&feature](const SolidObject& object) {
                return object.id == feature.outputId.toStdString();
            });
        for (auto& argument : feature.arguments) {
            double value = argument.evaluatedValue;
            if (!argument.parameterRef.isEmpty()) {
                const auto parameter = values.constFind(argument.parameterRef);
                if (parameter == values.cend()) {
                    invalid(QStringLiteral("Feature %1 references missing parameter %2.")
                                .arg(feature.id, argument.parameterRef));
                }
                value = parameter.value();
                argument.evaluatedValue = value;
            }
            if (solid != project->solids.end()) {
                if (argument.name == QStringLiteral("x")) solid->x = value;
                else if (argument.name == QStringLiteral("y")) solid->y = value;
                else if (argument.name == QStringLiteral("z")) solid->z = value;
                else if (argument.name == QStringLiteral("rotation_x"))
                    solid->rotationXDegrees = value;
                else if (argument.name == QStringLiteral("rotation_y"))
                    solid->rotationYDegrees = value;
                else if (argument.name == QStringLiteral("rotation_z"))
                    solid->rotationZDegrees = value;
                else if (argument.name == QStringLiteral("length"))
                    solid->length = value;
                else if (argument.name == QStringLiteral("width"))
                    solid->width = value;
                else if (argument.name == QStringLiteral("height"))
                    solid->height = value;
                else if (argument.name == QStringLiteral("radius"))
                    solid->radius = value;
                else if (argument.name == QStringLiteral("hole_radius"))
                    solid->holeRadius = value;
                else if (argument.name == QStringLiteral("relative_permittivity"))
                    solid->relativePermittivity = value;
                else if (argument.name == QStringLiteral("conductivity"))
                    solid->conductivity = value;
                else if (argument.name == QStringLiteral("loss_tangent"))
                    solid->lossTangent = value;
                else {
                    static const QRegularExpression pointExpression(
                        QStringLiteral("^point_(\\d+)_([xyz])$"));
                    const auto match = pointExpression.match(argument.name);
                    if (match.hasMatch()) {
                        const int index = match.captured(1).toInt();
                        if (index >= 0 &&
                            index < static_cast<int>(solid->curvePoints.size())) {
                            auto& point = solid->curvePoints[static_cast<size_t>(index)];
                            const QChar axis = match.captured(2).front();
                            if (axis == QLatin1Char('x')) point.x = value;
                            else if (axis == QLatin1Char('y')) point.y = value;
                            else point.z = value;
                        }
                    }
                }
            }

            if (!feature.materialRef.isEmpty()) {
                auto material = std::find_if(
                    project->materials.begin(), project->materials.end(),
                    [&feature](const OemMaterial& item) {
                        return item.id == feature.materialRef;
                    });
                if (material != project->materials.end()) {
                    if (argument.name ==
                        QStringLiteral("relative_permittivity")) {
                        material->relativePermittivity = value;
                    } else if (argument.name ==
                               QStringLiteral("conductivity")) {
                        material->conductivitySPerM = value;
                    } else if (argument.name ==
                               QStringLiteral("loss_tangent")) {
                        material->lossTangent = value;
                    } else if (argument.name ==
                               QStringLiteral("relative-permittivity")) {
                        material->relativePermittivity = value;
                    }
                }
            }
        }
        if (solid != project->solids.end() &&
            solid->kind == SolidKind::Curve &&
            solid->curvePoints.size() >= 2) {
            solid->customShape = makePolylineCurve(solid->curvePoints);
        }
    }
}

void readPackageFile(const QString& packageRoot, const QString& packagePath,
                     QByteArray* data) {
    const QString safePath = safeRelativePath(packagePath);
    QFile file(QDir(packageRoot).filePath(safePath));
    if (!file.open(QIODevice::ReadOnly)) {
        invalid(QStringLiteral("Cannot read project asset %1: %2")
                    .arg(safePath, file.errorString()));
    }
    *data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        invalid(QStringLiteral("Cannot read project asset %1: %2")
                    .arg(safePath, file.errorString()));
    }
}

void parseSolverFiles(QXmlStreamReader& reader, OemProjectDocument* project,
                      const QString& packageRoot) {
    if (requiredAttribute(reader.attributes(), QStringLiteral("format")) !=
        QStringLiteral("openEMS-CSXCAD-XML")) {
        invalid(QStringLiteral("Unsupported solver asset format."));
    }
    while (reader.readNextStartElement()) {
        if (reader.name() != QStringLiteral("file")) {
            invalid(QStringLiteral("Unknown solver asset element: %1")
                        .arg(reader.name().toString()));
        }
        const auto attributes = reader.attributes();
        const QString path = safeRelativePath(
            requiredAttribute(attributes, QStringLiteral("path")));
        if (!path.startsWith(QStringLiteral("models/"))) {
            invalid(QStringLiteral("Invalid native solver asset path: %1").arg(path));
        }
        const QString key = path.mid(QStringLiteral("models/").size());
        if (project->solverFiles.contains(key)) {
            invalid(QStringLiteral("Duplicate solver asset: %1").arg(path));
        }
        QByteArray bytes;
        readPackageFile(packageRoot, path, &bytes);
        if (bytes.isEmpty()) {
            invalid(QStringLiteral("Native solver asset is empty: %1").arg(path));
        }
        project->solverFiles.insert(key, bytes);
        reader.skipCurrentElement();
    }
}

void parseResults(QXmlStreamReader& reader, OemProjectDocument* project,
                  const QString& packageRoot) {
    project->resultKind = requiredAttribute(reader.attributes(), QStringLiteral("kind"));
    if (project->resultKind.isEmpty()) {
        invalid(QStringLiteral("Project result kind cannot be empty."));
    }
    while (reader.readNextStartElement()) {
        if (reader.name() != QStringLiteral("file")) {
            invalid(QStringLiteral("Unknown project results element: %1")
                        .arg(reader.name().toString()));
        }
        if (project->resultFiles.size() >= 100000) {
            invalid(QStringLiteral("Project contains too many result files."));
        }
        const auto attributes = reader.attributes();
        const QString path = safeRelativePath(
            requiredAttribute(attributes, QStringLiteral("path")));
        if (!path.startsWith(QStringLiteral("results/"))) {
            invalid(QStringLiteral("Invalid project result path: %1").arg(path));
        }
        if (isRawFieldDump(path)) {
            invalid(QStringLiteral("Raw field dumps are not included in OEM 0.0.1: %1")
                        .arg(path));
        }
        const QString key = path.mid(QStringLiteral("results/").size());
        if (project->resultFiles.contains(key)) {
            invalid(QStringLiteral("Duplicate project result file: %1").arg(path));
        }
        QByteArray bytes;
        readPackageFile(packageRoot, path, &bytes);
        project->resultFiles.insert(key, bytes);
        reader.skipCurrentElement();
    }
    if (project->resultKind == QStringLiteral("none") &&
        !project->resultFiles.isEmpty()) {
        invalid(QStringLiteral("A project without results cannot contain result files."));
    }
}

void validateSolid(const SolidObject& object) {
    const auto inRange = [](double value, double minimum, double maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if (object.name.empty() || !inRange(object.x, -1000, 1000) ||
        !inRange(object.y, -1000, 1000) || !inRange(object.z, -1000, 1000) ||
        !inRange(object.rotationXDegrees, -3600, 3600) ||
        !inRange(object.rotationYDegrees, -3600, 3600) ||
        !inRange(object.rotationZDegrees, -3600, 3600) ||
        !inRange(object.length, 0.1, 1000) ||
        !inRange(object.width, 0.1, 1000) ||
        !inRange(object.height, 0.1, 1000) ||
        !inRange(object.radius, 0.1, 500) ||
        !inRange(object.holeRadius, 0.1, 500) ||
        !inRange(object.relativePermittivity, 1, 1000) ||
        !inRange(object.conductivity, 0, 1e8) ||
        !inRange(object.lossTangent, 0, 10)) {
        invalid(QStringLiteral("Project contains invalid solid dimensions or material values."));
    }
    if (object.kind == SolidKind::PlateWithHole &&
        object.holeRadius * 2.0 >= std::min(object.length, object.width)) {
        invalid(QStringLiteral("A plate hole must be smaller than the plate footprint."));
    }
    if ((object.kind == SolidKind::Imported ||
         object.kind == SolidKind::Surface) &&
        object.customShape.IsNull()) {
        invalid(QStringLiteral("Imported shape %1 has no geometry.")
                    .arg(QString::fromStdString(object.name)));
    }
    if (object.kind == SolidKind::Curve) {
        if (object.curvePoints.size() < 2 || object.customShape.IsNull()) {
            invalid(QStringLiteral("Curve %1 needs at least two valid points.")
                        .arg(QString::fromStdString(object.name)));
        }
        for (const auto& point : object.curvePoints) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.z)) {
                invalid(QStringLiteral("Curve %1 has a non-finite point.")
                            .arg(QString::fromStdString(object.name)));
            }
        }
    }
}

void validateSimulation(const OemSimulationSettings& settings) {
    const auto inRange = [](double value, double minimum, double maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if (!inRange(settings.frequencyStartGHz, 0.001, 1000) ||
        !inRange(settings.frequencyStopGHz, settings.frequencyStartGHz + 0.001, 1000) ||
        settings.frequencyPoints < 2 || settings.frequencyPoints > 10001 ||
        settings.meshDivisions < 8 || settings.meshDivisions > 80 ||
        settings.maximumTimesteps < 1000 || settings.maximumTimesteps > 10000000 ||
        (settings.portAxis != QStringLiteral("x") &&
         settings.portAxis != QStringLiteral("y") &&
         settings.portAxis != QStringLiteral("z")) ||
        !inRange(settings.portStartXmm, -1000, 1000) ||
        !inRange(settings.portStartYmm, -1000, 1000) ||
        !inRange(settings.portStartZmm, -1000, 1000) ||
        !inRange(settings.portStopXmm, -1000, 1000) ||
        !inRange(settings.portStopYmm, -1000, 1000) ||
        !inRange(settings.portStopZmm, -1000, 1000) ||
        (settings.portAxis == QStringLiteral("x") &&
         (settings.portStartXmm >= settings.portStopXmm ||
          settings.portStartYmm != settings.portStopYmm ||
          settings.portStartZmm != settings.portStopZmm)) ||
        (settings.portAxis == QStringLiteral("y") &&
         (settings.portStartYmm >= settings.portStopYmm ||
          settings.portStartXmm != settings.portStopXmm ||
          settings.portStartZmm != settings.portStopZmm)) ||
        (settings.portAxis == QStringLiteral("z") &&
         (settings.portStartZmm >= settings.portStopZmm ||
          settings.portStartXmm != settings.portStopXmm ||
          settings.portStartYmm != settings.portStopYmm)) ||
        !inRange(settings.portImpedanceOhm, 1, 10000) ||
        (settings.boundary != QStringLiteral("MUR") &&
         settings.boundary != QStringLiteral("PML_8")) ||
        (settings.engine != QStringLiteral("cpu") &&
         settings.engine != QStringLiteral("gpu"))) {
        invalid(QStringLiteral("Project contains invalid simulation or model settings."));
    }
}

void validateDesign(const OemProjectDocument& project) {
    QSet<QString> parameterIds;
    for (const auto& parameter : project.parameters) {
        if (parameter.id.isEmpty() || parameter.name.isEmpty() ||
            parameter.unit.isEmpty() || parameter.expression.isEmpty() ||
            !std::isfinite(parameter.evaluatedValue) ||
            parameterIds.contains(parameter.id)) {
            invalid(QStringLiteral("Project contains an invalid or duplicate parameter."));
        }
        if (parameter.optimizable &&
            (!std::isfinite(parameter.minimum) ||
             !std::isfinite(parameter.maximum) ||
             !std::isfinite(parameter.step) ||
             parameter.minimum >= parameter.maximum ||
             parameter.step < 0.0 ||
             parameter.evaluatedValue < parameter.minimum ||
             parameter.evaluatedValue > parameter.maximum)) {
            invalid(QStringLiteral("Project contains invalid optimization bounds for %1.")
                        .arg(parameter.name));
        }
        parameterIds.insert(parameter.id);
    }

    QSet<QString> materialIds;
    for (const auto& material : project.materials) {
        if (material.id.isEmpty() || material.name.isEmpty() ||
            materialIds.contains(material.id) ||
            (material.type != QStringLiteral("pec") &&
             material.type != QStringLiteral("dielectric")) ||
            !std::isfinite(material.relativePermittivity) ||
            !std::isfinite(material.conductivitySPerM) ||
            !std::isfinite(material.lossTangent) ||
            material.relativePermittivity < 1.0 ||
            material.conductivitySPerM < 0.0 ||
            material.lossTangent < 0.0) {
            invalid(QStringLiteral("Project contains an invalid or duplicate material."));
        }
        materialIds.insert(material.id);
    }

    QSet<QString> featureIds;
    QSet<QString> outputIds;
    for (const auto& feature : project.featureHistory) {
        if (feature.id.isEmpty() || feature.type.isEmpty() ||
            feature.outputId.isEmpty() || feature.outputKind.isEmpty() ||
            featureIds.contains(feature.id) || outputIds.contains(feature.outputId)) {
            invalid(QStringLiteral("Project contains an invalid or duplicate feature."));
        }
        for (const QString& input : feature.inputs) {
            if (!outputIds.contains(input)) {
                invalid(QStringLiteral("Feature %1 references an unavailable prior output %2.")
                            .arg(feature.id, input));
            }
        }
        if (!feature.materialRef.isEmpty() &&
            !materialIds.contains(feature.materialRef)) {
            invalid(QStringLiteral("Feature %1 references an unknown material.")
                        .arg(feature.id));
        }
        for (const auto& argument : feature.arguments) {
            if (argument.name.isEmpty() || argument.expression.isEmpty() ||
                argument.unit.isEmpty() ||
                !std::isfinite(argument.evaluatedValue) ||
                (!argument.parameterRef.isEmpty() &&
                 !parameterIds.contains(argument.parameterRef))) {
                invalid(QStringLiteral("Feature %1 contains an invalid argument.")
                            .arg(feature.id));
            }
        }
        featureIds.insert(feature.id);
        outputIds.insert(feature.outputId);
    }

    for (const auto& solid : project.solids) {
        const QString id = QString::fromStdString(solid.id);
        const QString materialId = QString::fromStdString(solid.materialId);
        if (id.isEmpty() || !materialIds.contains(materialId) ||
            !outputIds.contains(id)) {
            invalid(QStringLiteral("Solid %1 is missing its feature or material link.")
                        .arg(QString::fromStdString(solid.name)));
        }
    }
}

}  // namespace

bool writeOemProject(const QString& path, const OemProjectDocument& project,
                     QString* error) {
    try {
        if (project.solids.size() > 10000) {
            invalid(QStringLiteral("Project contains too many solids."));
        }
        for (const auto& solid : project.solids) validateSolid(solid);
        validateSimulation(project.simulation);
        validateDesign(project);
        if (project.resultKind.isEmpty()) {
            invalid(QStringLiteral("Project result kind cannot be empty."));
        }
        if (project.resultKind == QStringLiteral("none") &&
            !project.resultFiles.isEmpty()) {
            invalid(QStringLiteral("A project without results cannot contain result files."));
        }

        QTemporaryDir packageDirectory;
        if (!packageDirectory.isValid()) {
            invalid(QStringLiteral("Cannot create temporary OEM package directory."));
        }
        const QString projectXmlPath =
            packageDirectory.filePath(QStringLiteral("project.xml"));
        QFile file(projectXmlPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            invalid(QStringLiteral("Cannot write project manifest: %1")
                        .arg(file.errorString()));
        }
        QXmlStreamWriter writer(&file);
        writer.setAutoFormatting(true);
        writer.writeStartDocument(QStringLiteral("1.0"));
        writer.writeStartElement(QStringLiteral("oem-project"));
        writer.writeAttribute(QStringLiteral("format-version"),
                              QString::fromLatin1(OemProjectFormatVersion));
        writer.writeStartElement(QStringLiteral("geometry"));
        for (size_t index = 0; index < project.solids.size(); ++index) {
            writeSolid(writer, project.solids[index], index, packageDirectory);
        }
        writer.writeEndElement();
        writeDesign(writer, project, packageDirectory);
        writeSimulation(writer, project.simulation);
        writer.writeStartElement(QStringLiteral("solver-assets"));
        writer.writeAttribute(QStringLiteral("format"),
                              QStringLiteral("openEMS-CSXCAD-XML"));
        for (auto it = project.solverFiles.cbegin();
             it != project.solverFiles.cend(); ++it) {
            const QString key = safeRelativePath(it.key());
            if (it.value().isEmpty()) {
                invalid(QStringLiteral("Invalid native solver asset: %1").arg(key));
            }
            const QString packagePath = QStringLiteral("models/") + key;
            writeAsset(packageDirectory.path(), packagePath, it.value());
            writer.writeStartElement(QStringLiteral("file"));
            writer.writeAttribute(QStringLiteral("path"), packagePath);
            writer.writeEndElement();
        }
        writer.writeEndElement();
        writer.writeStartElement(QStringLiteral("results"));
        writer.writeAttribute(QStringLiteral("kind"), project.resultKind);
        for (auto it = project.resultFiles.cbegin();
             it != project.resultFiles.cend(); ++it) {
            const QString relativePath = safeRelativePath(it.key());
            if (isRawFieldDump(relativePath)) {
                invalid(QStringLiteral("Raw field dumps are not included in OEM 0.0.1: %1")
                            .arg(relativePath));
            }
            if (it.value().isEmpty()) {
                invalid(QStringLiteral("Project result file is empty: %1")
                            .arg(relativePath));
            }
            const QString packagePath = QStringLiteral("results/") + relativePath;
            writeAsset(packageDirectory.path(), packagePath, it.value());
            writer.writeStartElement(QStringLiteral("file"));
            writer.writeAttribute(QStringLiteral("path"), packagePath);
            writer.writeEndElement();
        }
        writer.writeEndElement();
        writer.writeEndElement();
        writer.writeEndDocument();
        if (writer.hasError() || !file.flush() ||
            file.error() != QFileDevice::NoError) {
            invalid(QStringLiteral("Cannot write project manifest: %1")
                        .arg(path, file.errorString()));
        }
        file.close();
        runPackageTool({QStringLiteral("pack"), packageDirectory.path(), path});
        return true;
    } catch (const std::exception& exception) {
        setError(error, QString::fromUtf8(exception.what()));
        return false;
    }
}

bool readOemProject(const QString& path, OemProjectDocument* project,
                    QString* error) {
    if (!project) {
        setError(error, QStringLiteral("Project output is unavailable."));
        return false;
    }
    try {
        QTemporaryDir packageDirectory;
        if (!packageDirectory.isValid()) {
            invalid(QStringLiteral("Cannot create temporary OEM extraction directory."));
        }
        runPackageTool(
            {QStringLiteral("unpack"), path, packageDirectory.path()});
        QFile file(packageDirectory.filePath(QStringLiteral("project.xml")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            invalid(QStringLiteral("OEM archive is missing project.xml: %1")
                        .arg(file.errorString()));
        }
        QXmlStreamReader reader(&file);
        if (!reader.readNextStartElement() ||
            reader.name() != QStringLiteral("oem-project")) {
            invalid(QStringLiteral("Invalid OEM project XML root element."));
        }
        if (requiredAttribute(reader.attributes(), QStringLiteral("format-version")) !=
            QString::fromLatin1(OemProjectFormatVersion)) {
            invalid(QStringLiteral("Unsupported OEM project format version."));
        }

        OemProjectDocument loaded;
        bool hasGeometry = false;
        bool hasDesign = false;
        bool hasSimulation = false;
        bool hasSolverAssets = false;
        bool hasResults = false;
        while (reader.readNextStartElement()) {
            if (reader.name() == QStringLiteral("geometry") && !hasGeometry) {
                hasGeometry = true;
                parseGeometry(reader, &loaded, packageDirectory);
            } else if (reader.name() == QStringLiteral("design") && !hasDesign) {
                hasDesign = true;
                parseDesign(reader, &loaded, packageDirectory.path());
            } else if (reader.name() == QStringLiteral("simulation") &&
                       !hasSimulation) {
                hasSimulation = true;
                parseSimulation(reader, &loaded.simulation);
            } else if (reader.name() == QStringLiteral("solver-assets") &&
                       !hasSolverAssets) {
                hasSolverAssets = true;
                parseSolverFiles(reader, &loaded, packageDirectory.path());
            } else if (reader.name() == QStringLiteral("results") && !hasResults) {
                hasResults = true;
                parseResults(reader, &loaded, packageDirectory.path());
            } else {
                invalid(QStringLiteral("Unknown or duplicate OEM project section: %1")
                            .arg(reader.name().toString()));
            }
        }
        if (reader.hasError()) {
            invalid(QStringLiteral("Invalid OEM project XML: %1")
                        .arg(reader.errorString()));
        }
        if (!hasGeometry || !hasDesign || !hasSimulation ||
            !hasSolverAssets || !hasResults) {
            invalid(QStringLiteral("OEM project is missing a required section."));
        }
        QMap<QString, OemMaterial> materialsById;
        for (const auto& material : loaded.materials) {
            materialsById.insert(material.id, material);
        }
        for (auto& solid : loaded.solids) {
            const QString materialId = QString::fromStdString(solid.materialId);
            const auto material = materialsById.constFind(materialId);
            if (material == materialsById.cend()) {
                invalid(QStringLiteral("Solid %1 references a missing material.")
                            .arg(QString::fromStdString(solid.name)));
            }
            solid.material =
                material->type == QStringLiteral("pec")
                    ? MaterialKind::PerfectConductor
                    : MaterialKind::Dielectric;
            solid.relativePermittivity = material->relativePermittivity;
            solid.conductivity = material->conductivitySPerM;
            solid.lossTangent = material->lossTangent;
            const QString id = QString::fromStdString(solid.id);
            for (const auto& feature : loaded.featureHistory) {
                if (feature.outputId == id) {
                    solid.featureType = feature.type.toStdString();
                    solid.inputFeatureIds.clear();
                    for (const QString& input : feature.inputs) {
                        solid.inputFeatureIds.push_back(input.toStdString());
                    }
                    break;
                }
            }
        }
        applyEvaluatedParameters(&loaded);
        validateDesign(loaded);
        for (const auto& solid : loaded.solids) validateSolid(solid);
        validateSimulation(loaded.simulation);
        *project = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        setError(error, QString::fromUtf8(exception.what()));
        return false;
    }
}

}  // namespace rfsim
