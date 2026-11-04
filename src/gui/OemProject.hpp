#pragma once

#include "rfsim/Geometry.hpp"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include <vector>

namespace rfsim {

inline constexpr char OemProjectFormatVersion[] = "0.0.2";

struct OemPort {
    QString type = QStringLiteral("lumped");
    QString mode;
    QString axis = QStringLiteral("z");
    double startXmm = 0.0;
    double startYmm = 0.0;
    double startZmm = 0.0;
    double stopXmm = 0.0;
    double stopYmm = 0.0;
    double stopZmm = 1.0;
    double impedanceOhm = 50.0;
};

struct OemSimulationSettings {
    double frequencyStartGHz = 1.0;
    double frequencyStopGHz = 2.0;
    int frequencyPoints = 201;
    int meshDivisions = 30;
    int maximumTimesteps = 20000;
    QString boundary = QStringLiteral("MUR");
    std::vector<OemPort> ports;
    QString engine = QStringLiteral("cpu");
};

struct OemParameter {
    QString id;
    QString name;
    QString unit;
    QString expression;
    QString role = QStringLiteral("constant");
    double evaluatedValue = 0.0;
    bool optimizable = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
};

struct OemMaterial {
    QString id;
    QString name;
    QString type;
    double relativePermittivity = 1.0;
    double conductivitySPerM = 0.0;
    double lossTangent = 0.0;
};

struct OemFeatureArgument {
    QString name;
    QString expression;
    QString parameterRef;
    QString unit;
    double evaluatedValue = 0.0;
};

struct OemFeature {
    QString id;
    QString type;
    QString outputId;
    QString outputKind;
    QString materialRef;
    QStringList inputs;
    std::vector<OemFeatureArgument> arguments;
    TopoDS_Shape geometryCache;
};

struct OemProjectDocument {
    std::vector<SolidObject> solids;
    OemSimulationSettings simulation;
    std::vector<OemParameter> parameters;
    std::vector<OemMaterial> materials;
    std::vector<OemFeature> featureHistory;
    QMap<QString, QByteArray> solverFiles;
    QString resultKind = QStringLiteral("none");
    QMap<QString, QByteArray> resultFiles;
};

void applyOemParameterValues(OemProjectDocument* project);
bool writeOemProject(const QString& path, const OemProjectDocument& project,
                     QString* error);
bool readOemProject(const QString& path, OemProjectDocument* project,
                    QString* error);

}  // namespace rfsim
