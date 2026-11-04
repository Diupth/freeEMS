#pragma once

#include "rfsim/Geometry.hpp"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include <vector>

namespace rfsim {

inline constexpr char OemProjectFormatVersion[] = "0.0.1";

struct OemSimulationSettings {
    double frequencyStartGHz = 1.0;
    double frequencyStopGHz = 3.0;
    int frequencyPoints = 201;
    int meshDivisions = 30;
    int maximumTimesteps = 30000;
    QString boundary = QStringLiteral("MUR");
    QString portAxis = QStringLiteral("z");
    double portStartXmm = -2.0;
    double portStartYmm = 10.0;
    double portStartZmm = -1.0;
    double portStopXmm = -2.0;
    double portStopYmm = 10.0;
    double portStopZmm = 2.6;
    double portImpedanceOhm = 50.0;
    QString engine = QStringLiteral("cpu");
    QString waveguideMode = QStringLiteral("TE10");
    double waveguideFrequencyStartGHz = 8.2;
    double waveguideFrequencyStopGHz = 12.4;
    double waveguideWidthMm = 22.86;
    double waveguideHeightMm = 10.16;
    double waveguideLengthMm = 100.0;
    double waveguidePortDepthMm = 5.0;
    double antennaSubstrateWidthMm = 60.0;
    double antennaSubstrateLengthMm = 60.0;
    double antennaSubstrateThicknessMm = 1.524;
    double antennaPatchWidthMm = 32.0;
    double antennaPatchLengthMm = 40.0;
    double antennaFeedPositionMm = -6.0;
    double antennaRelativePermittivity = 3.38;
    double antennaConductivitySPerM = 0.0;
    double antennaLossTangent = 0.0;
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

bool writeOemProject(const QString& path, const OemProjectDocument& project,
                     QString* error);
bool readOemProject(const QString& path, OemProjectDocument* project,
                    QString* error);

}  // namespace rfsim
