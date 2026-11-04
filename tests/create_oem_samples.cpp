#include "OemProject.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextStream>

#include <cmath>
#include <stdexcept>

namespace {

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(
            QStringLiteral("Cannot read %1: %2")
                .arg(path, file.errorString()).toStdString());
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        throw std::runtime_error(
            QStringLiteral("Cannot read %1: %2")
                .arg(path, file.errorString()).toStdString());
    }
    return data;
}

void addParameter(rfsim::OemProjectDocument* project, const QString& id,
                  const QString& name, const QString& unit, double value,
                  bool optimizable = false, double minimum = 0.0,
                  double maximum = 0.0) {
    rfsim::OemParameter parameter;
    parameter.id = id;
    parameter.name = name;
    parameter.unit = unit;
    parameter.expression = QString::number(value, 'g', 17);
    parameter.evaluatedValue = value;
    parameter.role = optimizable ? QStringLiteral("design-variable")
                                 : QStringLiteral("constant");
    parameter.optimizable = optimizable;
    parameter.minimum = minimum;
    parameter.maximum = maximum;
    parameter.step = optimizable ? (maximum - minimum) / 100.0 : 0.0;
    project->parameters.push_back(parameter);
}

void addArgument(rfsim::OemFeature* feature, const QString& name,
                 const QString& parameterId, const QString& unit, double value) {
    feature->arguments.push_back({
        name, QString::number(value, 'g', 17), parameterId, unit, value
    });
}

void readSolverFiles(const QString& resultsDirectory,
                     rfsim::OemProjectDocument* project) {
    const QString modelsDirectory =
        QDir(resultsDirectory).filePath(QStringLiteral("models"));
    if (!QFileInfo(modelsDirectory).isDir()) {
        throw std::runtime_error(
            QStringLiteral("Native solver model directory is missing: %1")
                .arg(modelsDirectory).toStdString());
    }
    QDirIterator files(modelsDirectory, QDir::Files | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    const QDir root(QFileInfo(modelsDirectory).absoluteFilePath());
    while (files.hasNext()) {
        const QString filePath = files.next();
        const QString relativePath = QDir::fromNativeSeparators(
            root.relativeFilePath(QFileInfo(filePath).absoluteFilePath()));
        project->solverFiles.insert(relativePath, readFile(filePath));
    }
    if (project->solverFiles.isEmpty()) {
        throw std::runtime_error("No native openEMS model files were generated.");
    }
}

void readDerivedResults(const QString& resultsDirectory,
                        const QStringList& requiredFiles,
                        rfsim::OemProjectDocument* project) {
    for (const QString& name : requiredFiles) {
        const QString filePath = QDir(resultsDirectory).filePath(name);
        project->resultFiles.insert(name, readFile(filePath));
    }
    const QStringList summaries =
        QDir(resultsDirectory).entryList({QStringLiteral("summary*.json")},
                                         QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& name : summaries) {
        const QByteArray raw =
            readFile(QDir(resultsDirectory).filePath(name));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            throw std::runtime_error(
                QStringLiteral("Invalid simulation summary %1: %2")
                    .arg(name, parseError.errorString()).toStdString());
        }
        QJsonObject summary = document.object();
        summary.remove(QStringLiteral("simulation_directories"));
        summary.remove(QStringLiteral("antenna_field_outputs"));
        summary.insert(QStringLiteral("raw_field_dumps_in_oem"), false);
        project->resultFiles.insert(
            name, QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }
}

rfsim::OemProjectDocument waveguideProject(const QString& outputDirectory) {
    rfsim::OemProjectDocument project;
    project.resultKind = QStringLiteral("waveguide");
    auto& simulation = project.simulation;
    simulation.frequencyPoints = 201;
    simulation.meshDivisions = 30;
    simulation.maximumTimesteps = 20000;
    simulation.engine = QStringLiteral("cpu");
    constexpr double width = 22.86;
    constexpr double height = 10.16;
    constexpr double length = 100.0;
    constexpr double portDepth = 5.0;

    project.materials.push_back(
        {QStringLiteral("mat-waveguide-wall"), QStringLiteral("Waveguide wall"),
         QStringLiteral("pec"), 1.0, 0.0, 0.0});
    rfsim::OemFeature waveguide;
    waveguide.id = QStringLiteral("model-waveguide");
    waveguide.type = QStringLiteral("waveguide.rectangular");
    waveguide.outputId = waveguide.id;
    waveguide.outputKind = QStringLiteral("solver-model");
    waveguide.materialRef = QStringLiteral("mat-waveguide-wall");
    const auto addWaveguideValue = [&](const QString& name, double value,
                                       double minimum, double maximum) {
        const QString parameterId = QStringLiteral("p-waveguide-%1").arg(name);
        addParameter(&project, parameterId, QStringLiteral("waveguide.%1").arg(name),
                     QStringLiteral("mm"), value, true, minimum, maximum);
        addArgument(&waveguide, name, parameterId, QStringLiteral("mm"), value);
    };
    addWaveguideValue(QStringLiteral("width-a"), width,
                      18.0, 27.0);
    addWaveguideValue(QStringLiteral("height-b"), height,
                      8.0, 13.0);
    addWaveguideValue(QStringLiteral("length"), length,
                      60.0, 150.0);
    addWaveguideValue(QStringLiteral("port-depth"),
                      portDepth, 2.0, 10.0);
    project.featureHistory.push_back(std::move(waveguide));

    readSolverFiles(outputDirectory, &project);
    readDerivedResults(
        outputDirectory,
        {QStringLiteral("s_parameters_wr90.csv"),
         QStringLiteral("s_parameters_wr90.s2p"),
         QStringLiteral("vswr_wr90.csv"),
         QStringLiteral("waveform_wr90.csv")},
        &project);
    return project;
}

rfsim::OemProjectDocument antennaProject(const QString& outputDirectory) {
    rfsim::OemProjectDocument project;
    project.resultKind = QStringLiteral("antenna");
    auto& simulation = project.simulation;
    simulation.frequencyStartGHz = 1.0;
    simulation.frequencyStopGHz = 3.0;
    simulation.frequencyPoints = 51;
    simulation.meshDivisions = 25;
    simulation.maximumTimesteps = 30000;
    simulation.engine = QStringLiteral("cpu");
    constexpr double substrateWidth = 60.0;
    constexpr double substrateLength = 60.0;
    constexpr double substrateThickness = 1.524;
    constexpr double patchWidth = 32.0;
    constexpr double patchLength = 40.0;
    constexpr double feedPosition = -6.0;
    constexpr double substratePermittivity = 3.38;

    project.materials = {
        {QStringLiteral("mat-antenna-pec"), QStringLiteral("Antenna metal"),
         QStringLiteral("pec"), 1.0, 0.0, 0.0},
        {QStringLiteral("mat-substrate"), QStringLiteral("Substrate"),
         QStringLiteral("dielectric"), substratePermittivity, 0.0, 0.0}
    };
    const auto addAntennaFeature =
        [&](const QString& id, const QString& type, const QString& material,
            const QStringList& inputs = QStringList()) {
            rfsim::OemFeature feature;
            feature.id = id;
            feature.type = type;
            feature.outputId = id;
            feature.outputKind = QStringLiteral("solid");
            feature.materialRef = material;
            feature.inputs = inputs;
            project.featureHistory.push_back(std::move(feature));
        };
    const auto addAntennaValue =
        [&](rfsim::OemFeature* feature, const QString& name,
            const QString& unit, double value, double minimum, double maximum,
            bool optimize) {
            const QString parameterId =
                QStringLiteral("p-%1-%2").arg(feature->id, name);
            addParameter(&project, parameterId,
                         QStringLiteral("antenna.%1").arg(name), unit,
                         value, optimize, minimum, maximum);
            addArgument(feature, name, parameterId, unit, value);
        };

    addAntennaFeature(QStringLiteral("antenna-substrate"),
                      QStringLiteral("primitive.box"),
                      QStringLiteral("mat-substrate"));
    auto& substrate = project.featureHistory.back();
    addAntennaValue(&substrate, QStringLiteral("width"),
                    QStringLiteral("mm"), substrateWidth,
                    50.0, 80.0, false);
    addAntennaValue(&substrate, QStringLiteral("length"),
                    QStringLiteral("mm"), substrateLength,
                    50.0, 80.0, false);
    addAntennaValue(&substrate, QStringLiteral("thickness"),
                    QStringLiteral("mm"), substrateThickness,
                    0.8, 2.0, false);
    addAntennaValue(&substrate, QStringLiteral("relative-permittivity"),
                    QStringLiteral("1"), substratePermittivity,
                    2.0, 5.0, true);

    addAntennaFeature(QStringLiteral("antenna-patch"),
                      QStringLiteral("primitive.box"),
                      QStringLiteral("mat-antenna-pec"),
                      {QStringLiteral("antenna-substrate")});
    auto& patch = project.featureHistory.back();
    addAntennaValue(&patch, QStringLiteral("width"), QStringLiteral("mm"),
                    patchWidth, 25.0, 38.0, true);
    addAntennaValue(&patch, QStringLiteral("length"), QStringLiteral("mm"),
                    patchLength, 30.0, 48.0, true);
    addAntennaValue(&patch, QStringLiteral("feed-position"),
                    QStringLiteral("mm"), feedPosition,
                    -12.0, -2.0, true);

    readSolverFiles(outputDirectory, &project);
    readDerivedResults(
        outputDirectory,
        {QStringLiteral("s11_antenna.csv"),
         QStringLiteral("s11_antenna.s1p"),
         QStringLiteral("far_field_antenna.csv"),
         QStringLiteral("near_field_antenna.csv")},
        &project);
    return project;
}

void verifyProject(const QString& path, const QString& expectedKind,
                   int expectedMinimumModels) {
    rfsim::OemProjectDocument loaded;
    QString error;
    if (!rfsim::readOemProject(path, &loaded, &error)) {
        throw std::runtime_error(error.toStdString());
    }
    if (loaded.resultKind != expectedKind ||
        loaded.solverFiles.size() < expectedMinimumModels ||
        loaded.resultFiles.isEmpty() || loaded.parameters.empty() ||
        loaded.featureHistory.empty()) {
        throw std::runtime_error(
            QStringLiteral("Generated OEM sample failed round-trip validation: %1")
                .arg(path).toStdString());
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (argc != 4) {
        QTextStream(stderr) << "Usage: create_oem_samples OUTPUT_DIR "
                               "WAVEGUIDE_RESULTS_DIR ANTENNA_RESULTS_DIR\n";
        return 2;
    }
    try {
        const QString outputDirectory = QString::fromLocal8Bit(argv[1]);
        const QString waveguideDirectory = QString::fromLocal8Bit(argv[2]);
        const QString antennaDirectory = QString::fromLocal8Bit(argv[3]);
        if (!QDir().mkpath(outputDirectory)) {
            throw std::runtime_error("Cannot create sample output directory.");
        }
        const QString waveguidePath =
            QDir(outputDirectory).filePath(QStringLiteral("waveguide.oem"));
        const QString antennaPath =
            QDir(outputDirectory).filePath(QStringLiteral("antenna.oem"));
        QString error;
        if (!rfsim::writeOemProject(
                waveguidePath, waveguideProject(waveguideDirectory), &error) ||
            !rfsim::writeOemProject(
                antennaPath, antennaProject(antennaDirectory), &error)) {
            throw std::runtime_error(error.toStdString());
        }
        verifyProject(waveguidePath, QStringLiteral("waveguide"), 2);
        verifyProject(antennaPath, QStringLiteral("antenna"), 1);
        QTextStream(stdout) << "Created and verified:\n"
                            << "  " << waveguidePath << '\n'
                            << "  " << antennaPath << '\n';
    } catch (const std::exception& exception) {
        QTextStream(stderr) << exception.what() << '\n';
        return 1;
    }
    return 0;
}
