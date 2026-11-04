#include "ModelView.hpp"
#include "rfsim/Geometry.hpp"
#include "OemProject.hpp"

#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCloseEvent>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QSaveFile>
#include <QProgressBar>
#include <QProcess>
#include <QProxyStyle>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStatusBar>
#include <QWindow>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

class LeftAlignedTabStyle final : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override {
        if (hint == SH_TabBar_Alignment) {
            return Qt::AlignLeft;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QDoubleSpinBox* makeNumberControl(double minimum, double maximum, double value,
                                  const QString& suffix = QString()) {
    auto* control = new QDoubleSpinBox;
    control->setRange(minimum, maximum);
    control->setDecimals(2);
    control->setSingleStep(1.0);
    control->setValue(value);
    control->setSuffix(suffix);
    return control;
}

QString kindName(rfsim::SolidKind kind) {
    switch (kind) {
    case rfsim::SolidKind::PlateWithHole: return QStringLiteral("Plate with hole");
    case rfsim::SolidKind::Box: return QStringLiteral("Box");
    case rfsim::SolidKind::Cylinder: return QStringLiteral("Cylinder");
    case rfsim::SolidKind::Imported: return QStringLiteral("Imported / Boolean");
    case rfsim::SolidKind::Curve: return QStringLiteral("Curve");
    case rfsim::SolidKind::Surface: return QStringLiteral("Surface");
    }
    return QStringLiteral("Solid");
}

class PlotWidget final : public QWidget {
public:
    explicit PlotWidget(QString title, QString xLabel, QString yLabel,
                        QWidget* parent = nullptr)
        : QWidget(parent), title_(std::move(title)), xLabel_(std::move(xLabel)),
          yLabel_(std::move(yLabel)) {
        setMinimumSize(420, 220);
    }

    void loadCsv(const QString& path, int xColumn, int yColumn,
                 double xScale, double yScale, int secondYColumn = -1,
                 QString firstLabel = QStringLiteral("incident"),
                 QString secondLabel = QStringLiteral("reflected")) {
        points_.clear();
        secondPoints_.clear();
        firstLabel_ = std::move(firstLabel);
        secondLabel_ = std::move(secondLabel);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!file.atEnd()) {
                const auto fields = QString::fromUtf8(file.readLine()).trimmed().split(',');
                if (fields.size() <= std::max(xColumn, yColumn)) {
                    continue;
                }
                bool xOk = false;
                bool yOk = false;
                const double x = fields[xColumn].toDouble(&xOk) * xScale;
                const double y = fields[yColumn].toDouble(&yOk) * yScale;
                if (xOk && yOk && std::isfinite(x) && std::isfinite(y)) {
                    points_.emplace_back(x, y);
                }
                if (secondYColumn >= 0 && fields.size() > secondYColumn) {
                    bool secondOk = false;
                    const double second =
                        fields[secondYColumn].toDouble(&secondOk) * yScale;
                    if (xOk && secondOk && std::isfinite(x) &&
                        std::isfinite(second)) {
                        secondPoints_.emplace_back(x, second);
                    }
                }
            }
        }
        update();
    }

    void clear() {
        points_.clear();
        secondPoints_.clear();
        update();
    }

    bool hasData() const { return !points_.empty(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);
        painter.setPen(QColor(35, 45, 59));
        painter.drawText(QRect(8, 8, width() - 16, 24),
                         Qt::AlignCenter, title_);
        const QRectF plot(64, 42, width() - 92, height() - 92);
        painter.setPen(QColor(120, 130, 145));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.bottomLeft(), plot.topLeft());
        painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 26),
                         Qt::AlignCenter, xLabel_);
        painter.save();
        painter.translate(18, plot.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-plot.height() / 2, -10, plot.height(), 20),
                         Qt::AlignCenter, yLabel_);
        painter.restore();
        if (points_.size() < 2) {
            painter.drawText(plot, Qt::AlignCenter,
                             QStringLiteral("Run a simulation to load data"));
            return;
        }
        double xMin = points_.front().x();
        double xMax = xMin;
        double yMin = points_.front().y();
        double yMax = yMin;
        for (const auto& point : points_) {
            xMin = std::min(xMin, point.x());
            xMax = std::max(xMax, point.x());
            yMin = std::min(yMin, point.y());
            yMax = std::max(yMax, point.y());
        }
        for (const auto& point : secondPoints_) {
            xMin = std::min(xMin, point.x());
            xMax = std::max(xMax, point.x());
            yMin = std::min(yMin, point.y());
            yMax = std::max(yMax, point.y());
        }
        if (xMax <= xMin) xMax = xMin + 1.0;
        if (yMax <= yMin) yMax = yMin + 1.0;
        const auto mapPoint = [&](const QPointF& point) {
            return QPointF(plot.left() + (point.x() - xMin) / (xMax - xMin) * plot.width(),
                           plot.bottom() - (point.y() - yMin) / (yMax - yMin) * plot.height());
        };
        painter.setPen(QColor(45, 116, 190));
        QPolygonF curve;
        for (const auto& point : points_) curve.append(mapPoint(point));
        painter.drawPolyline(curve);
        if (secondPoints_.size() > 1) {
            painter.setPen(QColor(220, 91, 47));
            QPolygonF secondCurve;
            for (const auto& point : secondPoints_) secondCurve.append(mapPoint(point));
            painter.drawPolyline(secondCurve);
            painter.drawText(QRectF(plot.left() + 8, plot.top() + 4, 150, 18),
                             Qt::AlignLeft, QStringLiteral("Blue: %1").arg(firstLabel_));
            painter.setPen(QColor(220, 91, 47));
            painter.drawText(QRectF(plot.left() + 8, plot.top() + 22, 150, 18),
                             Qt::AlignLeft, QStringLiteral("Red: %1").arg(secondLabel_));
        }
        painter.setPen(QColor(70, 77, 89));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 1, 55, 18),
                         Qt::AlignLeft, QString::number(xMin, 'g', 4));
        painter.drawText(QRectF(plot.right() - 55, plot.bottom() + 1, 55, 18),
                         Qt::AlignRight, QString::number(xMax, 'g', 4));
        painter.drawText(QRectF(8, plot.bottom() - 10, 48, 18),
                         Qt::AlignRight, QString::number(yMin, 'g', 4));
        painter.drawText(QRectF(8, plot.top() - 8, 48, 18),
                         Qt::AlignRight, QString::number(yMax, 'g', 4));
    }

private:
    QString title_;
    QString xLabel_;
    QString yLabel_;
    QString firstLabel_ = QStringLiteral("incident");
    QString secondLabel_ = QStringLiteral("reflected");
    std::vector<QPointF> points_;
    std::vector<QPointF> secondPoints_;
};

class FieldMapWidget final : public QWidget {
public:
    explicit FieldMapWidget(QString title, QString unit, QWidget* parent = nullptr)
        : QWidget(parent), title_(std::move(title)), unit_(std::move(unit)) {
        setMinimumSize(420, 220);
    }

    void loadCsv(const QString& path, int xColumn, int yColumn, int valueColumn) {
        samples_.clear();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            file.readLine();
            while (!file.atEnd()) {
                const auto fields =
                    QString::fromUtf8(file.readLine()).trimmed().split(',');
                if (fields.size() <= std::max({xColumn, yColumn, valueColumn})) continue;
                bool xOk = false, yOk = false, valueOk = false;
                Sample sample{
                    fields[xColumn].toDouble(&xOk),
                    fields[yColumn].toDouble(&yOk),
                    fields[valueColumn].toDouble(&valueOk)
                };
                if (xOk && yOk && valueOk && std::isfinite(sample.x) &&
                    std::isfinite(sample.y) && std::isfinite(sample.value) &&
                    sample.value >= 0.0) {
                    samples_.push_back(sample);
                }
            }
        }
        update();
    }

    void clear() {
        samples_.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(35, 45, 59));
        painter.drawText(QRect(8, 8, width() - 16, 24), Qt::AlignCenter, title_);
        const QRectF plot(64, 42, width() - 92, height() - 92);
        painter.setPen(QColor(120, 130, 145));
        painter.drawRect(plot);
        if (samples_.empty()) {
            painter.drawText(plot, Qt::AlignCenter,
                             QStringLiteral("No field slice loaded"));
            return;
        }
        double xMin = samples_.front().x, xMax = xMin;
        double yMin = samples_.front().y, yMax = yMin;
        double vMin = std::numeric_limits<double>::max();
        double vMax = 0.0;
        std::vector<double> xs, ys;
        for (const auto& sample : samples_) {
            xMin = std::min(xMin, sample.x);
            xMax = std::max(xMax, sample.x);
            yMin = std::min(yMin, sample.y);
            yMax = std::max(yMax, sample.y);
            const double level = std::log10(std::max(sample.value, 1e-30));
            vMin = std::min(vMin, level);
            vMax = std::max(vMax, level);
            xs.push_back(sample.x);
            ys.push_back(sample.y);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
        const double cellWidth = plot.width() / std::max<size_t>(1, xs.size());
        const double cellHeight = plot.height() / std::max<size_t>(1, ys.size());
        const double levelSpan = std::max(vMax - vMin, 1e-12);
        for (const auto& sample : samples_) {
            const double level =
                std::log10(std::max(sample.value, 1e-30));
            const double normalized = std::clamp((level - vMin) / levelSpan, 0.0, 1.0);
            const QColor color = QColor::fromHsvF((1.0 - normalized) * 0.66, 0.9, 0.95);
            const double x = plot.left() +
                (sample.x - xMin) / std::max(xMax - xMin, 1e-12) * plot.width();
            const double y = plot.bottom() -
                (sample.y - yMin) / std::max(yMax - yMin, 1e-12) * plot.height();
            painter.fillRect(QRectF(x - cellWidth * 0.5, y - cellHeight * 0.5,
                                    cellWidth + 1.0, cellHeight + 1.0), color);
        }
        painter.setPen(QColor(55, 62, 74));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 22),
                         Qt::AlignCenter,
                         QStringLiteral("x/y (mm)  |  color: log10(%1)").arg(unit_));
        painter.drawText(QRectF(plot.left() + 8, plot.top() + 5, 200, 18),
                         Qt::AlignLeft, QStringLiteral("%1 … %2 %3")
                             .arg(vMin, 0, 'f', 2).arg(vMax, 0, 'f', 2).arg(unit_));
    }

private:
    struct Sample { double x; double y; double value; };
    QString title_;
    QString unit_;
    std::vector<Sample> samples_;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("RF Simulation — No Project"));
        resize(1280, 800);
        if (const auto* screen = QApplication::primaryScreen()) {
            const QRect available = screen->availableGeometry();
            const int width = std::min(1280, available.width() * 9 / 10);
            const int height = std::min(800, available.height() * 9 / 10);
            resize(width, height);
            move(available.center() - QPoint(width / 2, height / 2));
        }
        const auto setButtonIcon = [this](QPushButton* button,
                                          QStyle::StandardPixmap icon) {
            button->setIcon(style()->standardIcon(icon));
            button->setIconSize(QSize(18, 18));
        };

        auto* central = new QWidget;
        auto* root = new QHBoxLayout(central);
        root->setContentsMargins(0, 0, 0, 0);
        auto* left = new QWidget;
        navigationPanel_ = left;
        auto* leftLayout = new QVBoxLayout(left);
        auto* addPlate = new QPushButton(this);
        auto* addBox = new QPushButton(this);
        auto* addCylinder = new QPushButton(this);
        auto* drawCurve = new QPushButton(this);
        addPlate->hide();
        addBox->hide();
        addCylinder->hide();
        drawCurve->hide();
        objectsList_ = new QTreeWidget;
        objectsList_->setHeaderHidden(true);
        objectsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        auto* removeButton = new QPushButton(QStringLiteral("Remove selected"));
        auto* duplicateButton = new QPushButton(QStringLiteral("Duplicate selected"));
        setButtonIcon(removeButton, QStyle::SP_TrashIcon);
        setButtonIcon(duplicateButton, QStyle::SP_FileIcon);
        leftLayout->addWidget(objectsList_, 1);
        leftLayout->addWidget(duplicateButton);
        leftLayout->addWidget(removeButton);

        auto* properties = new QWidget;
        auto* propertyLayout = new QVBoxLayout(properties);
        auto* form = new QFormLayout;
        x_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        y_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        z_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        curvePlaneZ_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        length_ = makeNumberControl(0.1, 1000, 40, QStringLiteral(" mm"));
        width_ = makeNumberControl(0.1, 1000, 20, QStringLiteral(" mm"));
        height_ = makeNumberControl(0.1, 1000, 1.6, QStringLiteral(" mm"));
        radius_ = makeNumberControl(0.1, 500, 5, QStringLiteral(" mm"));
        holeRadius_ = makeNumberControl(0.1, 500, 3, QStringLiteral(" mm"));
        rotationX_ = makeNumberControl(-3600, 3600, 0, QStringLiteral("°"));
        rotationY_ = makeNumberControl(-3600, 3600, 0, QStringLiteral("°"));
        rotationZ_ = makeNumberControl(-3600, 3600, 0, QStringLiteral("°"));
        form->addRow(QStringLiteral("Origin X"), x_);
        form->addRow(QStringLiteral("Origin Y"), y_);
        form->addRow(QStringLiteral("Origin Z"), z_);
        form->addRow(QStringLiteral("Curve plane Z"), curvePlaneZ_);
        form->addRow(QStringLiteral("Length"), length_);
        form->addRow(QStringLiteral("Width"), width_);
        form->addRow(QStringLiteral("Height"), height_);
        form->addRow(QStringLiteral("Cylinder radius"), radius_);
        form->addRow(QStringLiteral("Hole radius"), holeRadius_);
        form->addRow(QStringLiteral("Rotation X"), rotationX_);
        form->addRow(QStringLiteral("Rotation Y"), rotationY_);
        form->addRow(QStringLiteral("Rotation Z"), rotationZ_);
        propertyLayout->addLayout(form);

        auto* exportStepButton = new QPushButton(QStringLiteral("Export scene as STEP"));
        setButtonIcon(exportStepButton, QStyle::SP_DialogSaveButton);
        propertyLayout->addWidget(exportStepButton);
        auto* sweepSurfaceButton =
            new QPushButton(QStringLiteral("Sweep curves → surface"));
        auto* sweepSolidButton =
            new QPushButton(QStringLiteral("Sweep closed profile → solid"));
        auto* solidifyShellButton =
            new QPushButton(QStringLiteral("Convert closed surface → solid"));
        propertyLayout->addWidget(sweepSurfaceButton);
        propertyLayout->addWidget(sweepSolidButton);
        propertyLayout->addWidget(solidifyShellButton);
        auto* newProjectButton = new QPushButton(QStringLiteral("New Project"));
        auto* openProjectButton = new QPushButton(QStringLiteral("Open Project"));
        auto* saveProjectButton = new QPushButton(QStringLiteral("Save Project"));
        auto* geometryHelp = new QLabel(QStringLiteral(
            "Draw Curve: click points; click the first point to close, then right-click or double-click to finish. Select two curves to sweep; close a shell before converting it to a solid for volume-based solver export."));
        geometryHelp->setWordWrap(true);
        propertyLayout->addWidget(geometryHelp);

        view_ = new ModelView;
        s11Plot_ = new PlotWidget(QStringLiteral("S11 (reflection coefficient)"),
                                  QStringLiteral("Frequency (GHz)"),
                                  QStringLiteral("S11 (dB)"));
        wavePlot_ = new PlotWidget(QStringLiteral("Port voltage waveform"),
                                   QStringLiteral("Time (ns)"),
                                   QStringLiteral("Voltage (V)"));
        resultPlots_ = new QTabWidget;
        resultPlots_->addTab(s11Plot_, QStringLiteral("S-parameters"));
        vswrPlot_ = new PlotWidget(QStringLiteral("Voltage standing-wave ratio"),
                                   QStringLiteral("Frequency (GHz)"),
                                   QStringLiteral("VSWR"));
        resultPlots_->addTab(vswrPlot_, QStringLiteral("VSWR"));
        resultPlots_->addTab(wavePlot_, QStringLiteral("Waveform"));
        auto* right = new QWidget;
        auto* rightLayout = new QVBoxLayout(right);
        rightLayout->addWidget(view_, 2);

        left->setMinimumWidth(210);
        left->setMaximumWidth(260);
        properties->setMinimumWidth(260);
        auto* propertiesScroll = new QScrollArea;
        propertiesScroll->setWidgetResizable(true);
        propertiesScroll->setFrameShape(QFrame::NoFrame);
        propertiesScroll->setMinimumWidth(260);
        propertiesScroll->setWidget(properties);
        editorSplitter_ = new QSplitter(Qt::Horizontal);
        editorSplitter_->setHandleWidth(7);
        editorSplitter_->setChildrenCollapsible(false);
        editorSplitter_->addWidget(propertiesScroll);
        editorSplitter_->addWidget(right);
        editorSplitter_->setStretchFactor(0, 0);
        editorSplitter_->setStretchFactor(1, 1);
        editorSplitter_->setSizes({320, 780});
        root->addWidget(editorSplitter_);

        workspace_ = new QStackedWidget;
        workspace_->addWidget(central);
        auto* materialsPage = new QWidget;
        auto* materialsLayout = new QVBoxLayout(materialsPage);
        auto* materialsForm = new QFormLayout;
        selectedMaterialLabel_ = new QLabel(QStringLiteral("No solid selected"));
        materialKind_ = new QComboBox;
        materialKind_->addItem(QStringLiteral("Perfect conductor (PEC)"),
                               static_cast<int>(rfsim::MaterialKind::PerfectConductor));
        materialKind_->addItem(QStringLiteral("Dielectric"),
                               static_cast<int>(rfsim::MaterialKind::Dielectric));
        relativePermittivity_ = makeNumberControl(1.0, 1000.0, 4.4);
        conductivity_ = makeNumberControl(0.0, 1.0e8, 0.0, QStringLiteral(" S/m"));
        lossTangent_ = makeNumberControl(0.0, 10.0, 0.0);
        conductivity_->setDecimals(6);
        lossTangent_->setDecimals(6);
        materialsForm->addRow(QStringLiteral("Selected solid"), selectedMaterialLabel_);
        materialsForm->addRow(QStringLiteral("Material model"), materialKind_);
        materialsForm->addRow(QStringLiteral("Relative permittivity εr"),
                              relativePermittivity_);
        materialsForm->addRow(QStringLiteral("Conductivity σ"), conductivity_);
        materialsForm->addRow(QStringLiteral("Loss tangent tanδ"), lossTangent_);
        materialsLayout->addWidget(
            new QLabel(QStringLiteral("Materials in this project")));
        projectMaterialsList_ = new QTableWidget(0, 5);
        projectMaterialsList_->setHorizontalHeaderLabels(
            {QStringLiteral("Material"), QStringLiteral("Type"),
             QStringLiteral("Relative permittivity"),
             QStringLiteral("Conductivity (S/m)"),
             QStringLiteral("Loss tangent")});
        projectMaterialsList_->horizontalHeader()->setStretchLastSection(true);
        projectMaterialsList_->verticalHeader()->hide();
        projectMaterialsList_->setSelectionBehavior(
            QAbstractItemView::SelectRows);
        projectMaterialsList_->setEditTriggers(
            QAbstractItemView::DoubleClicked |
            QAbstractItemView::EditKeyPressed |
            QAbstractItemView::SelectedClicked);
        projectMaterialsList_->setMaximumHeight(150);
        materialsLayout->addWidget(projectMaterialsList_);
        materialsLayout->addLayout(materialsForm);
        materialsLayout->addWidget(new QLabel(
            QStringLiteral("Dielectric conductivity and loss tangent are converted to openEMS electric loss (κ) at the band center. PEC ignores these values.")));
        materialsLayout->addStretch();
        auto* materialsScroll = new QScrollArea;
        materialsScroll->setWidgetResizable(true);
        materialsScroll->setWidget(materialsPage);
        workspace_->addWidget(materialsScroll);

        auto* simulationPage = new QWidget;
        auto* simulationLayout = new QVBoxLayout(simulationPage);
        auto* simulationForm = new QFormLayout;
        frequencyStart_ = makeNumberControl(0.001, 1000.0, 1.0, QStringLiteral(" GHz"));
        frequencyStop_ = makeNumberControl(0.002, 1000.0, 2.0, QStringLiteral(" GHz"));
        frequencyPoints_ = makeNumberControl(2, 10001, 201);
        frequencyPoints_->setDecimals(0);
        meshDivisions_ = makeNumberControl(8, 80, 30);
        meshDivisions_->setDecimals(0);
        maxTimesteps_ = makeNumberControl(1000, 10000000, 20000);
        maxTimesteps_->setDecimals(0);
        boundary_ = new QComboBox;
        boundary_->addItem(QStringLiteral("MUR"), QStringLiteral("MUR"));
        boundary_->addItem(QStringLiteral("PML_8"), QStringLiteral("PML_8"));
        portStartX_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        portStartY_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        portStartZ_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        portStopX_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        portStopY_ = makeNumberControl(-1000, 1000, 0, QStringLiteral(" mm"));
        portStopZ_ = makeNumberControl(-1000, 1000, 1, QStringLiteral(" mm"));
        portDirection_ = new QComboBox;
        portDirection_->addItem(QStringLiteral("X"), QStringLiteral("x"));
        portDirection_->addItem(QStringLiteral("Y"), QStringLiteral("y"));
        portDirection_->addItem(QStringLiteral("Z"), QStringLiteral("z"));
        portDirection_->setCurrentIndex(
            portDirection_->findData(QStringLiteral("z")));
        portImpedance_ = makeNumberControl(1, 10000, 50, QStringLiteral(" Ω"));
        simulationForm->addRow(QStringLiteral("Frequency start"), frequencyStart_);
        simulationForm->addRow(QStringLiteral("Frequency stop"), frequencyStop_);
        simulationForm->addRow(QStringLiteral("S11 samples"), frequencyPoints_);
        simulationForm->addRow(QStringLiteral("Mesh cells / wavelength"), meshDivisions_);
        simulationForm->addRow(QStringLiteral("Maximum time steps"), maxTimesteps_);
        simulationForm->addRow(QStringLiteral("Boundary condition"), boundary_);
        simulationForm->addRow(QStringLiteral("Lumped port axis"), portDirection_);
        simulationForm->addRow(QStringLiteral("Start X"), portStartX_);
        simulationForm->addRow(QStringLiteral("Start Y"), portStartY_);
        simulationForm->addRow(QStringLiteral("Start Z"), portStartZ_);
        simulationForm->addRow(QStringLiteral("Stop X"), portStopX_);
        simulationForm->addRow(QStringLiteral("Stop Y"), portStopY_);
        simulationForm->addRow(QStringLiteral("Stop Z"), portStopZ_);
        simulationForm->addRow(QStringLiteral("Port impedance"), portImpedance_);
        simulationLayout->addLayout(simulationForm);
        auto* simulationHelp = new QLabel(QStringLiteral(
            "CAD workflow: draw/select geometry → assign material → set frequency, mesh and boundary → configure the lumped port → Run simulation."));
        simulationHelp->setWordWrap(true);
        simulationLayout->addWidget(simulationHelp);
        auto* engineRow = new QHBoxLayout;
        engine_ = new QComboBox;
        engine_->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
        engine_->addItem(QStringLiteral("GPU (if available)"), QStringLiteral("gpu"));
        runButton_ = new QPushButton(QStringLiteral("Run simulation"));
        cancelButton_ = new QPushButton(QStringLiteral("Cancel simulation"));
        setButtonIcon(runButton_, QStyle::SP_MediaPlay);
        setButtonIcon(cancelButton_, QStyle::SP_MediaStop);
        cancelButton_->setEnabled(false);
        engineRow->addWidget(engine_);
        engineRow->addWidget(runButton_);
        engineRow->addWidget(cancelButton_);
        simulationLayout->addLayout(engineRow);
        status_ = new QLabel(QStringLiteral("Ready"));
        status_->setWordWrap(true);
        simulationLayout->addWidget(status_);
        simulationLayout->addWidget(new QLabel(
            QStringLiteral("Use a port that crosses a feed gap; do not place it through metal. All current solids are homogeneous and isotropic. Results depend on the mesh, material model and boundary settings.")));
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(3000);
        progressBar_ = new QProgressBar;
        progressBar_->setRange(0, 0);
        progressBar_->hide();
        simulationLayout->addWidget(new QLabel(
            QStringLiteral("Detailed solver output is shown in Progress / Messages below.")));
        simulationLayout->addStretch();
        auto* simulationScroll = new QScrollArea;
        simulationScroll->setWidgetResizable(true);
        simulationScroll->setWidget(simulationPage);
        workspace_->addWidget(simulationScroll);
        workspace_->setMinimumSize(0, 0);

        parameterTable_ = new QTableWidget(0, 5);
        parameterTable_->setHorizontalHeaderLabels(
            {QStringLiteral("Name"), QStringLiteral("Expression"),
             QStringLiteral("Value"), QStringLiteral("Unit"),
             QStringLiteral("Description")});
        parameterTable_->setEditTriggers(
            QAbstractItemView::DoubleClicked |
            QAbstractItemView::EditKeyPressed |
            QAbstractItemView::SelectedClicked);
        parameterTable_->horizontalHeader()->setStretchLastSection(true);
        parameterTable_->verticalHeader()->setDefaultSectionSize(21);
        parameterTable_->verticalHeader()->setMinimumSectionSize(18);
        parameterTable_->setAlternatingRowColors(true);
        updateParameterList();
        connect(parameterTable_, &QTableWidget::itemChanged, this,
                [this](QTableWidgetItem* item) {
            updateProjectParameter(item);
        });
        connect(projectMaterialsList_, &QTableWidget::itemChanged, this,
                [this](QTableWidgetItem* item) {
            updateProjectMaterial(item);
        });
        resultsPage_ = new QWidget;
        auto* resultsLayout = new QVBoxLayout(resultsPage_);
        projectResultsList_ = new QTreeWidget;
        projectResultsList_->setHeaderHidden(true);
        resultPlots_->addTab(projectResultsList_, QStringLiteral("Project Files"));
        // Hidden helper: the ribbon/menu "Open Results Folder" actions trigger it.
        auto* openResultsButton = new QPushButton(resultsPage_);
        openResultsButton->hide();
        resultsLayout->addWidget(resultPlots_, 1);
        workspace_->addWidget(resultsPage_);
        solverProcess_ = new QProcess(this);

        auto addObject = [this](rfsim::SolidKind kind, const QString& name) {
            rfsim::SolidObject object;
            object.kind = kind;
            object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            object.featureType = kind == rfsim::SolidKind::Box
                                     ? "primitive.box"
                                     : kind == rfsim::SolidKind::Cylinder
                                           ? "primitive.cylinder"
                                           : "primitive.plate-with-hole";
            object.name = name.toStdString() +
                          " " + std::to_string(objects_.size() + 1);
            object.x = objects_.size() * 5.0;
            object.y = objects_.size() * 5.0;
            recordGeometryFeature(object);
            objects_.push_back(object);
            refreshList(static_cast<int>(objects_.size() - 1));
            loadSelected();
            loadSelectedMaterial();
        };
        connect(addPlate, &QPushButton::clicked, this, [addObject] {
            addObject(rfsim::SolidKind::PlateWithHole, QStringLiteral("Plate"));
        });
        connect(addBox, &QPushButton::clicked, this, [addObject] {
            addObject(rfsim::SolidKind::Box, QStringLiteral("Box"));
        });
        connect(addCylinder, &QPushButton::clicked, this, [addObject] {
            addObject(rfsim::SolidKind::Cylinder, QStringLiteral("Cylinder"));
        });
        connect(drawCurve, &QPushButton::clicked, this,
                [this] { beginCurveDrawing(); });
        connect(sweepSurfaceButton, &QPushButton::clicked, this,
                [this] { sweepSelectedCurvesToSurface(); });
        connect(sweepSolidButton, &QPushButton::clicked, this,
                [this] { sweepSelectedFaceToSolid(); });
        connect(solidifyShellButton, &QPushButton::clicked, this,
                [this] { solidifySelectedSurface(); });
        connect(removeButton, &QPushButton::clicked, this, [this] {
            const int row = currentObjectIndex();
            if (row < 0 || row >= static_cast<int>(objects_.size())) return;
            objects_.erase(objects_.begin() + row);
            refreshList(std::min(row, static_cast<int>(objects_.size()) - 1));
            loadSelected();
        });
        connect(duplicateButton, &QPushButton::clicked, this,
                [this] { duplicateSelected(); });
        connect(objectsList_, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            loadSelected();
            loadSelectedMaterial();
            if (!current) return;
            const auto* section = current;
            while (section->parent()) section = section->parent();
            const QString sectionName = section->text(0);
            if (sectionName == QStringLiteral("Materials")) {
                workspace_->setCurrentIndex(1);
            } else if (sectionName == QStringLiteral("Excitations")) {
                workspace_->setCurrentIndex(2);
            } else if (sectionName == QStringLiteral("Results")) {
                workspace_->setCurrentIndex(3);
            } else {
                workspace_->setCurrentIndex(0);
            }
        });
        view_->setSelectionHandler([this](int index) {
            if (index < 0 || !objectsList_->topLevelItemCount()) {
                objectsList_->clearSelection();
                objectsList_->setCurrentItem(nullptr);
                return;
            }
            auto* geometry = objectsList_->topLevelItem(0);
            if (index < geometry->childCount()) {
                objectsList_->clearSelection();
                objectsList_->setCurrentItem(geometry->child(index));
            }
        });
        for (auto* control : {x_, y_, z_, length_, width_, height_, radius_,
                              holeRadius_, rotationX_, rotationY_, rotationZ_}) {
            connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
                updateSelected();
            });
        }
        connect(curvePlaneZ_, &QDoubleSpinBox::valueChanged, this,
                [this](double z) { view_->setCurvePlaneZ(z); });
        connect(materialKind_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { updateSelectedMaterial(); });
        for (auto* control : {relativePermittivity_, conductivity_, lossTangent_}) {
            connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
                updateSelectedMaterial();
            });
        }
        connect(runButton_, &QPushButton::clicked, this, [this] { runSimulation(); });
        connect(cancelButton_, &QPushButton::clicked, this,
                [this] { cancelSimulation(); });
        connect(newProjectButton, &QPushButton::clicked, this,
                [this] { newProject(); });
        connect(openProjectButton, &QPushButton::clicked, this,
                [this] { openProject(); });
        connect(saveProjectButton, &QPushButton::clicked, this,
                [this] { saveProject(); });
        for (auto* control : {portStartX_, portStartY_, portStartZ_,
                              portStopX_, portStopY_, portStopZ_}) {
            connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
                updatePortVisualization();
                updateParameterList();
            });
        }
        connect(portDirection_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) {
            updatePortVisualization();
            updateParameterList();
        });
        for (auto* control : {frequencyStart_, frequencyStop_, frequencyPoints_,
                              meshDivisions_, maxTimesteps_, portImpedance_}) {
            connect(control, &QDoubleSpinBox::valueChanged, this, [this] {
                updateParameterList();
            });
        }
        connect(boundary_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { updateParameterList(); });
        connect(exportStepButton, &QPushButton::clicked, this,
                [this] { exportStep(); });
        connect(openResultsButton, &QPushButton::clicked, this, [this] {
            if (!resultDirectory_.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(resultDirectory_));
            }
        });
        connect(solverProcess_, &QProcess::readyReadStandardOutput, this, [this] {
            log_->appendPlainText(QString::fromUtf8(solverProcess_->readAllStandardOutput()));
        });
        connect(solverProcess_, &QProcess::readyReadStandardError, this, [this] {
            log_->appendPlainText(QString::fromUtf8(solverProcess_->readAllStandardError()));
        });
        connect(solverProcess_,
                qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus exitStatus) {
            runButton_->setEnabled(true);
            cancelButton_->setEnabled(false);
            progressBar_->setRange(0, 100);
            progressBar_->setValue(100);
            QTimer::singleShot(1200, progressBar_, &QWidget::hide);
            if (cancelRequested_) {
                cancelRequested_ = false;
                status_->setText(QStringLiteral("Simulation cancelled."));
                statusBar()->showMessage(QStringLiteral("Simulation cancelled"));
                log_->appendPlainText(QStringLiteral("Simulation cancelled by user."));
                if (smokeTestMode_) {
                    smokeTestExitCode_ = smokeTestExpectedCancel_ ? 0 : 1;
                    QApplication::quit();
                }
                return;
            }
            const QString output = log_->toPlainText();
            const bool gpuRequested =
                engine_->currentData().toString() == QStringLiteral("gpu");
            const bool gpuConfirmed =
                output.contains(QStringLiteral("enabled GPU engine"),
                               Qt::CaseInsensitive);
            const bool resultsExist =
                QFileInfo::exists(resultDirectory_ + QStringLiteral("/s11_scene.csv")) &&
                QFileInfo::exists(resultDirectory_ + QStringLiteral("/s11_scene.s1p")) &&
                QFileInfo::exists(resultDirectory_ + QStringLiteral("/waveform_scene.csv"));
            if (exitStatus == QProcess::NormalExit && code == 0 &&
                (!gpuRequested || gpuConfirmed) && resultsExist) {
                if (smokeTestExpectedCancel_) smokeTestExitCode_ = 1;
                status_->setText(QStringLiteral("Simulation completed; results loaded below."));
                s11Plot_->loadCsv(resultDirectory_ + QStringLiteral("/s11_scene.csv"),
                                  0, 3, 1e-9, 1.0);
                vswrPlot_->clear();
                wavePlot_->loadCsv(resultDirectory_ + QStringLiteral("/waveform_scene.csv"),
                                   0, 1, 1e9, 1.0, 2);
                statusBar()->showMessage(QStringLiteral("Simulation complete — results loaded"));
                refreshList(currentObjectIndex());
                workspace_->setCurrentWidget(resultsPage_);
            } else {
                smokeTestExitCode_ = 1;
                status_->setText(QStringLiteral(
                    "Simulation failed, results are missing, or the requested GPU engine was not confirmed."));
                log_->appendPlainText(QStringLiteral("Process exit code: %1").arg(code));
                statusBar()->showMessage(QStringLiteral("Simulation failed — see Progress / Messages"));
            }
            if (smokeTestMode_) QApplication::quit();
        });
        connect(solverProcess_, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
            runButton_->setEnabled(true);
            cancelButton_->setEnabled(false);
            progressBar_->hide();
            status_->setText(QStringLiteral("Could not start solver."));
            log_->appendPlainText(solverProcess_->errorString());
            if (smokeTestMode_) {
                smokeTestExitCode_ = 1;
                QApplication::quit();
            }
        });

        auto* drawPlateButton = new QPushButton(QStringLiteral("Draw Plate"));
        auto* drawBoxButton = new QPushButton(QStringLiteral("Draw Box"));
        auto* topViewButton = new QPushButton(QStringLiteral("Top / 3D View"));
        auto* importStepButton = new QPushButton(QStringLiteral("Import STEP"));
        auto* fuseButton = new QPushButton(QStringLiteral("Boolean Fuse"));
        auto* cutButton = new QPushButton(QStringLiteral("Boolean Cut"));
        auto* gridButton = new QPushButton(QStringLiteral("Grid"));
        gridButton->setCheckable(true);
        gridButton->setChecked(true);
        auto* fitViewButton = new QPushButton(QStringLiteral("Fit View"));
        connect(drawPlateButton, &QPushButton::clicked, this, [this] {
            beginRectangleDrawing(rfsim::SolidKind::PlateWithHole);
        });
        connect(drawBoxButton, &QPushButton::clicked, this, [this] {
            beginRectangleDrawing(rfsim::SolidKind::Box);
        });
        connect(topViewButton, &QPushButton::clicked, this, [this] {
            view_->setDrawMode({});
            topViewEnabled_ = !topViewEnabled_;
            if (topViewEnabled_) view_->setTopView(true);
            else view_->resetCamera();
        });
        connect(importStepButton, &QPushButton::clicked, this,
                [this] { importStep(); });
        connect(fuseButton, &QPushButton::clicked, this,
                [this] { booleanFuseSelected(); });
        connect(cutButton, &QPushButton::clicked, this,
                [this] { booleanCutSelected(); });
        connect(gridButton, &QPushButton::toggled, this, [this](bool visible) {
            gridVisible_ = visible;
            view_->setShowGrid(visible);
        });
        connect(fitViewButton, &QPushButton::clicked, this, [this] {
            view_->resetCamera();
            topViewEnabled_ = false;
        });
        createRibbonAndMenus(addPlate, addBox, addCylinder, drawCurve, exportStepButton,
                             openResultsButton, drawPlateButton, drawBoxButton,
                             topViewButton, newProjectButton, openProjectButton,
                             saveProjectButton, duplicateButton, importStepButton,
                             fuseButton, cutButton, gridButton, fitViewButton);
        refreshList(-1);
        loadSelected();
        loadSelectedMaterial();
    }

    void ensureSmokeGeometry() {
        if (!objects_.empty()) return;
        rfsim::SolidObject plate;
        plate.name = "Smoke plate";
        plate.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        plate.featureType = "primitive.plate-with-hole";
        recordGeometryFeature(plate);
        objects_.push_back(std::move(plate));
        refreshList(0);
        loadSelected();
        loadSelectedMaterial();
    }

    void recordGeometryFeature(rfsim::SolidObject& object) {
        if (object.id.empty()) {
            object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        }
        if (object.featureType.empty()) {
            object.featureType = object.kind == rfsim::SolidKind::Box
                                     ? "primitive.box"
                                     : object.kind == rfsim::SolidKind::Cylinder
                                           ? "primitive.cylinder"
                                           : object.kind == rfsim::SolidKind::Imported
                                                 ? "step.import"
                                                 : "primitive.plate-with-hole";
        }
        if (object.materialId.empty()) {
            object.materialId = "mat-" + object.id;
        }
        const QString id = QString::fromStdString(object.id);
        const QString parameterPrefix = QStringLiteral("p-%1-").arg(id);
        geometryParameters_.erase(
            std::remove_if(geometryParameters_.begin(), geometryParameters_.end(),
                           [&parameterPrefix](const rfsim::OemParameter& parameter) {
                               return parameter.id.startsWith(parameterPrefix);
                           }),
            geometryParameters_.end());
        const QString materialId = QString::fromStdString(object.materialId);
        geometryMaterials_.erase(
            std::remove_if(geometryMaterials_.begin(), geometryMaterials_.end(),
                           [&materialId](const rfsim::OemMaterial& material) {
                               return material.id == materialId;
                           }),
            geometryMaterials_.end());
        rfsim::OemMaterial material;
        material.id = materialId;
        material.name = QString::fromStdString(object.name) +
                        QStringLiteral(" material");
        material.type = object.material == rfsim::MaterialKind::Dielectric
                            ? QStringLiteral("dielectric")
                            : QStringLiteral("pec");
        material.relativePermittivity = object.relativePermittivity;
        material.conductivitySPerM = object.conductivity;
        material.lossTangent = object.lossTangent;
        geometryMaterials_.push_back(material);

        auto featureIt = std::find_if(
            geometryHistory_.begin(), geometryHistory_.end(),
            [&id](const rfsim::OemFeature& feature) { return feature.id == id; });
        if (featureIt == geometryHistory_.end()) {
            geometryHistory_.push_back({});
            featureIt = std::prev(geometryHistory_.end());
        }
        featureIt->id = id;
        featureIt->type = QString::fromStdString(object.featureType);
        featureIt->outputId = id;
        featureIt->outputKind = object.kind == rfsim::SolidKind::Curve
                                    ? QStringLiteral("curve")
                                    : object.kind == rfsim::SolidKind::Surface
                                          ? QStringLiteral("surface")
                                          : QStringLiteral("solid");
        featureIt->materialRef = materialId;
        featureIt->inputs.clear();
        for (const auto& input : object.inputFeatureIds) {
            featureIt->inputs.push_back(QString::fromStdString(input));
        }
        featureIt->geometryCache =
            object.kind == rfsim::SolidKind::Imported ||
                    object.kind == rfsim::SolidKind::Curve ||
                    object.kind == rfsim::SolidKind::Surface
                ? rfsim::makeSolid(object)
                : TopoDS_Shape();
        featureIt->arguments.clear();
        const auto addArgument = [&](const QString& name, const QString& unit,
                                     double value) {
            const QString parameterId = parameterPrefix + name;
            rfsim::OemParameter parameter;
            parameter.id = parameterId;
            parameter.name = QString::fromStdString(object.name) +
                             QLatin1Char('.') + name;
            parameter.unit = unit;
            parameter.expression = QString::number(value, 'g', 17);
            parameter.evaluatedValue = value;
            geometryParameters_.push_back(parameter);
            featureIt->arguments.push_back(
                {name, parameter.expression, parameterId, unit, value});
        };
        addArgument(QStringLiteral("x"), QStringLiteral("mm"), object.x);
        addArgument(QStringLiteral("y"), QStringLiteral("mm"), object.y);
        addArgument(QStringLiteral("z"), QStringLiteral("mm"), object.z);
        addArgument(QStringLiteral("rotation_x"), QStringLiteral("deg"),
                    object.rotationXDegrees);
        addArgument(QStringLiteral("rotation_y"), QStringLiteral("deg"),
                    object.rotationYDegrees);
        addArgument(QStringLiteral("rotation_z"), QStringLiteral("deg"),
                    object.rotationZDegrees);
        addArgument(QStringLiteral("length"), QStringLiteral("mm"), object.length);
        addArgument(QStringLiteral("width"), QStringLiteral("mm"), object.width);
        addArgument(QStringLiteral("height"), QStringLiteral("mm"), object.height);
        addArgument(QStringLiteral("radius"), QStringLiteral("mm"), object.radius);
        addArgument(QStringLiteral("hole_radius"), QStringLiteral("mm"),
                    object.holeRadius);
        addArgument(QStringLiteral("relative_permittivity"), QStringLiteral("1"),
                    object.relativePermittivity);
        addArgument(QStringLiteral("conductivity"), QStringLiteral("S/m"),
                    object.conductivity);
        addArgument(QStringLiteral("loss_tangent"), QStringLiteral("1"),
                    object.lossTangent);
        for (size_t index = 0; index < object.curvePoints.size(); ++index) {
            const auto& point = object.curvePoints[index];
            const QString pointName =
                QStringLiteral("point_%1").arg(index);
            addArgument(pointName + QStringLiteral("_x"), QStringLiteral("mm"),
                        point.x);
            addArgument(pointName + QStringLiteral("_y"), QStringLiteral("mm"),
                        point.y);
            addArgument(pointName + QStringLiteral("_z"), QStringLiteral("mm"),
                        point.z);
        }
        refreshProjectMaterialList();
        updateParameterList();
    }

    void startSimulationForSmokeTest() {
        smokeTestMode_ = true;
        ensureSmokeGeometry();
        runSimulation(QString::fromUtf8(RF_SIM_SMOKE_OUTPUT));
    }

    void startXAxisPortSmokeTest() {
        smokeTestMode_ = true;
        ensureSmokeGeometry();
        portDirection_->setCurrentIndex(
            portDirection_->findData(QStringLiteral("x")));
        portStartX_->setValue(-20.0);
        portStartY_->setValue(10.0);
        portStartZ_->setValue(10.0);
        portStopX_->setValue(-15.0);
        portStopY_->setValue(10.0);
        portStopZ_->setValue(10.0);
        runSimulation(QString::fromUtf8(RF_SIM_AXIS_PORT_SMOKE_OUTPUT));
    }

    void startDrawSmokeTest() {
        beginRectangleDrawing(rfsim::SolidKind::Box);
        const QPointF start(view_->width() * 0.25, view_->height() * 0.25);
        const QPointF stop(view_->width() * 0.6, view_->height() * 0.6);
        QMouseEvent press(QEvent::MouseButtonPress, start, start, start,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view_, &press);
        QMouseEvent move(QEvent::MouseMove, stop, stop, stop, Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view_, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, stop, stop, stop,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view_, &release);
        smokeTestExitCode_ =
            objects_.size() == 1 && objects_.back().kind == rfsim::SolidKind::Box &&
                    objects_.back().length > 0.5 && objects_.back().width > 0.5
                ? 0
                : 1;
        QApplication::quit();
    }

    void startCurveDrawSmokeTest() {
        beginCurveDrawing();
        QApplication::processEvents();
        const auto click = [this](const QPointF& point) {
            QMouseEvent press(QEvent::MouseButtonPress, point, point, point,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view_, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, point, point, point,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view_, &release);
        };
        const QPointF first(view_->width() * 0.25, view_->height() * 0.3);
        const QPointF second(view_->width() * 0.65, view_->height() * 0.35);
        const QPointF third(view_->width() * 0.55, view_->height() * 0.72);
        click(first);
        curvePlaneZ_->setValue(5.0);
        click(second);
        curvePlaneZ_->setValue(0.0);
        click(third);
        click(first);
        smokeTestExitCode_ =
            objects_.size() == 1 &&
                    objects_.front().kind == rfsim::SolidKind::Curve &&
                    objects_.front().curvePoints.size() == 4 &&
                    std::abs(objects_.front().curvePoints[1].z -
                             objects_.front().curvePoints[0].z - 5.0) < 1e-9 &&
                    !objects_.front().customShape.IsNull() &&
                    BRep_Tool::IsClosed(
                        TopoDS::Wire(objects_.front().customShape))
                ? 0
                : 1;
        QApplication::quit();
    }

    void startDirectPickSmokeTest() {
        workspace_->setCurrentWidget(workspace_->widget(0));
        ensureSmokeGeometry();
        objectsList_->setCurrentItem(objectsList_->topLevelItem(0));
        QApplication::processEvents();
        const int step = 10;
        for (int y = step; y < view_->height() &&
                           currentObjectIndex() != 0; y += step) {
            for (int x = step; x < view_->width() &&
                               currentObjectIndex() != 0; x += step) {
                const QPointF position(x, y);
                QMouseEvent press(QEvent::MouseButtonPress, position, position,
                                  position, Qt::LeftButton, Qt::LeftButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(view_, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, position, position,
                                    position, Qt::LeftButton, Qt::NoButton,
                                    Qt::NoModifier);
                QApplication::sendEvent(view_, &release);
            }
        }
        smokeTestExitCode_ = currentObjectIndex() == 0 ? 0 : 1;
        QApplication::quit();
    }

    void startProjectIoSmokeTest() {
        QTemporaryDir directory;
        if (!directory.isValid()) {
            smokeTestExitCode_ = 1;
            QApplication::quit();
            return;
        }
        ensureSmokeGeometry();
        projectPath_ = directory.filePath(QStringLiteral("roundtrip.oem"));
        QString error;
        rfsim::SolidObject profile;
        profile.kind = rfsim::SolidKind::Curve;
        profile.name = "Sweep profile";
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        profile.featureType = "curve.polyline";
        profile.curvePoints = {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}};
        profile.customShape = rfsim::makePolylineCurve(profile.curvePoints);
        recordGeometryFeature(profile);
        objects_.push_back(profile);
        rfsim::SolidObject path;
        path.kind = rfsim::SolidKind::Curve;
        path.name = "Sweep path";
        path.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        path.featureType = "curve.polyline";
        path.curvePoints = {{0, 0, 0}, {0, 0, 5}};
        path.customShape = rfsim::makePolylineCurve(path.curvePoints);
        recordGeometryFeature(path);
        objects_.push_back(path);
        auto surface = makeSurfaceObject(
            rfsim::sweepCurveToSurface(
                TopoDS::Wire(rfsim::makeSolid(profile)),
                TopoDS::Wire(rfsim::makeSolid(path))),
            QStringLiteral("Sweep test surface"));
        surface.inputFeatureIds = {profile.id, path.id};
        recordGeometryFeature(surface);
        objects_.push_back(surface);
        auto imported = makeImportedObject(
            rfsim::makeSolid(objects_.front()), QStringLiteral("Imported test"));
        imported.rotationZDegrees = 15.0;
        recordGeometryFeature(imported);
        objects_.push_back(std::move(imported));
        const QString resultDirectory =
            directory.filePath(QStringLiteral("sample-results"));
        if (!QDir().mkpath(resultDirectory)) {
            smokeTestExitCode_ = 1;
            QApplication::quit();
            return;
        }
        QFile s11(resultDirectory + QStringLiteral("/s11_scene.csv"));
        QFile s11Touchstone(resultDirectory + QStringLiteral("/s11_scene.s1p"));
        QFile waveform(resultDirectory + QStringLiteral("/waveform_scene.csv"));
        if (!s11.open(QIODevice::WriteOnly | QIODevice::Text) ||
            s11.write("frequency_hz,s11_real,s11_imag,s11_db\n1,0,0,-20\n") < 0 ||
            !s11Touchstone.open(QIODevice::WriteOnly | QIODevice::Text) ||
            s11Touchstone.write(
                "[Version] 2.1\n# Hz S RI R 50\n[Number of Ports] 1\n"
                "[Number of Frequencies] 1\n[Reference] 50\n[Network Data]\n"
                "1000000000 0.1 0\n[End]\n") < 0 ||
            !waveform.open(QIODevice::WriteOnly | QIODevice::Text) ||
            waveform.write("time_s,incident_voltage_v,reflected_voltage_v\n0,1,0.1\n") < 0) {
            smokeTestExitCode_ = 1;
            QApplication::quit();
            return;
        }
        QFile rawField(resultDirectory + QStringLiteral("/raw_field.h5"));
        const QString modelDirectory =
            resultDirectory + QStringLiteral("/models/geometry");
        if (!QDir().mkpath(modelDirectory) ||
            !rawField.open(QIODevice::WriteOnly) ||
            rawField.write("raw field fixture") < 0) {
            smokeTestExitCode_ = 1;
            QApplication::quit();
            return;
        }
        rawField.close();
        QFile solverModel(resultDirectory + QStringLiteral("/models/openems.xml"));
        QFile solverMesh(modelDirectory + QStringLiteral("/solid_0.stl"));
        if (!solverModel.open(QIODevice::WriteOnly | QIODevice::Text) ||
            solverModel.write("<openEMS/>\n") < 0 ||
            !solverMesh.open(QIODevice::WriteOnly) ||
            solverMesh.write("mesh fixture") < 0) {
            smokeTestExitCode_ = 1;
            QApplication::quit();
            return;
        }
        resultDirectory_ = resultDirectory;
        resultKind_ = QStringLiteral("cad");
        projectResultFiles_.clear();
        const auto addHistoryParameter = [&](const QString& id, const QString& name,
                                             double value) {
            rfsim::OemParameter parameter;
            parameter.id = id;
            parameter.name = name;
            parameter.unit = QStringLiteral("mm");
            parameter.expression = QString::number(value, 'g', 17);
            parameter.evaluatedValue = value;
            geometryParameters_.push_back(parameter);
            return parameter.id;
        };
        const QString curveLengthParameter = addHistoryParameter(
            QStringLiteral("p-test-curve-length"),
            QStringLiteral("test.curve_length"), 8.0);
        const auto addHistoryFeature = [&](const QString& id, const QString& type,
                                           const QString& outputKind,
                                           const QStringList& inputs,
                                           const QString& materialRef = QString()) {
            rfsim::OemFeature feature;
            feature.id = id;
            feature.type = type;
            feature.outputId = id;
            feature.outputKind = outputKind;
            feature.inputs = inputs;
            feature.materialRef = materialRef;
            geometryHistory_.push_back(std::move(feature));
        };
        addHistoryFeature(QStringLiteral("test-profile"),
                          QStringLiteral("curve.line"), QStringLiteral("curve"), {});
        geometryHistory_.back().arguments.push_back(
            {QStringLiteral("length"), QStringLiteral("8"),
             curveLengthParameter, QStringLiteral("mm"), 8.0});
        addHistoryFeature(QStringLiteral("test-path"),
                          QStringLiteral("curve.line"), QStringLiteral("curve"), {});
        addHistoryFeature(QStringLiteral("test-surface"),
                          QStringLiteral("sweep.surface"), QStringLiteral("surface"),
                          {QStringLiteral("test-profile"),
                           QStringLiteral("test-path")});
        addHistoryFeature(QStringLiteral("test-solid"),
                          QStringLiteral("solidify.closed-shell"),
                          QStringLiteral("solid"),
                          {QStringLiteral("test-surface")},
                          geometryMaterials_.front().id);
        const double editedParameterValue =
            geometryParameters_.front().evaluatedValue + 0.5;
        parameterTable_->item(0, 2)->setText(
            QString::number(editedParameterValue, 'g', 10));
        const QString editedMaterialName =
            geometryMaterials_.front().name + QStringLiteral(" edited");
        projectMaterialsList_->item(0, 0)->setText(editedMaterialName);
        const auto originalObjects = objects_;
        const double originalFrequency = frequencyStart_->value();
        const bool saved = saveProjectFile(projectPath_, &error);
        objects_.clear();
        frequencyStart_->setValue(99.0);
        resultDirectory_.clear();
        const bool loaded = saved && loadProjectFile(projectPath_, &error);
        QFile embeddedResult(resultDirectory_ + QStringLiteral("/s11_scene.csv"));
        const bool resultRestored = embeddedResult.open(QIODevice::ReadOnly) &&
            embeddedResult.readAll().contains("frequency_hz") &&
            !QFileInfo::exists(resultDirectory_ + QStringLiteral("/raw_field.h5")) &&
            solverFiles_.contains(QStringLiteral("openems.xml")) &&
            solverFiles_.contains(QStringLiteral("geometry/solid_0.stl"));
        bool roundTripOkay =
            loaded && objects_.size() == originalObjects.size() &&
            frequencyStart_->value() == originalFrequency &&
            !geometryParameters_.empty() &&
            parameterTable_->rowCount() ==
                static_cast<int>(geometryParameters_.size()) &&
            parameterTable_->item(0, 0) &&
            parameterTable_->item(0, 0)->text() ==
                geometryParameters_.front().name &&
            geometryParameters_.front().evaluatedValue ==
                editedParameterValue &&
            geometryMaterials_.front().name == editedMaterialName &&
            projectMaterialsList_->item(0, 0)->text() == editedMaterialName &&
            objects_[1].kind == rfsim::SolidKind::Curve &&
            objects_[1].curvePoints.size() == 3 &&
            objects_[3].kind == rfsim::SolidKind::Surface &&
            !objects_[3].customShape.IsNull() &&
            objects_.back().kind == rfsim::SolidKind::Imported &&
            !objects_.back().customShape.IsNull() &&
            std::abs(objects_.back().rotationZDegrees - 15.0) < 1e-9 &&
            resultRestored &&
            std::any_of(geometryHistory_.begin(), geometryHistory_.end(),
                        [](const rfsim::OemFeature& feature) {
                            return feature.type ==
                                QStringLiteral("solidify.closed-shell");
                        });
        smokeTestExitCode_ = roundTripOkay ? 0 : 1;
        QApplication::quit();
    }

    void startEmptyStartupSmokeTest() {
        const bool emptyStartup = objects_.empty() && resultDirectory_.isEmpty() &&
                                  !hasActiveProject_;
        if (emptyStartup) newProject();
        smokeTestExitCode_ =
            emptyStartup && hasActiveProject_ && objects_.empty() &&
                    windowTitle() == QStringLiteral("RF Simulation — Untitled Project")
                ? 0
                : 1;
        QApplication::quit();
    }

    void startOpenProjectSmokeTest(const QString& path) {
        QString error;
        const bool loaded = !path.isEmpty() && loadProjectFile(path, &error);
        auto* ribbon = findChild<QTabWidget*>(QStringLiteral("mainRibbon"));
        const auto ribbonButtons =
            ribbon ? ribbon->findChildren<QToolButton*>() : QList<QToolButton*>{};
        const auto openProjectButton =
            std::find_if(ribbonButtons.cbegin(), ribbonButtons.cend(),
                         [](const QToolButton* button) {
                             return button->text() ==
                                    QStringLiteral("Open Project");
                         });
        const bool consistentRibbonWidths =
            openProjectButton != ribbonButtons.cend() &&
            std::all_of(ribbonButtons.cbegin(), ribbonButtons.cend(),
                        [openProjectButton](const QToolButton* button) {
                            return button->text().isEmpty() ||
                                   button->width() == (*openProjectButton)->width();
                        });
        const auto solverOptionsButton =
            std::find_if(ribbonButtons.cbegin(), ribbonButtons.cend(),
                         [](const QToolButton* button) {
                             return button->text() ==
                                    QStringLiteral("Solver Options");
                         });
        const bool hasSolverOptions =
            solverOptionsButton != ribbonButtons.cend();
        QWidget* workspaceBeforeSolverOptions =
            workspace_->currentWidget();
        if (solverOptionsButton != ribbonButtons.cend()) {
            (*solverOptionsButton)->click();
        }
        const bool solverOptionsDoesNothing =
            workspace_->currentWidget() == workspaceBeforeSolverOptions;
        const QMenu* solverMenu =
            findChild<QMenu*>(QStringLiteral("solverMenu"));
        const QMenu* domainMenu =
            findChild<QMenu*>(QStringLiteral("domainMenu"));
        const bool projectSectionsPopulated =
            objectsList_->topLevelItemCount() == 4 &&
            objectsList_->topLevelItem(0)->childCount() > 0 &&
            objectsList_->topLevelItem(1)->childCount() > 0 &&
            objectsList_->topLevelItem(2)->childCount() > 0 &&
            objectsList_->topLevelItem(3)->childCount() > 0 &&
            projectMaterialsList_->rowCount() > 0 &&
            projectResultsList_->topLevelItemCount() > 0;
        const bool projectParametersPopulated =
            !geometryParameters_.empty() &&
            parameterTable_->rowCount() ==
                static_cast<int>(geometryParameters_.size()) &&
            parameterTable_->item(0, 0) &&
            parameterTable_->item(0, 0)->text() ==
                geometryParameters_.front().name;
        const bool resultGraphsVisible =
            s11Plot_->hasData() || vswrPlot_->hasData() || wavePlot_->hasData();
        smokeTestExitCode_ =
            loaded && !solverFiles_.isEmpty() &&
                    projectSectionsPopulated &&
                    projectParametersPopulated &&
                    resultGraphsVisible &&
                    workspace_->currentWidget() == workspace_->widget(0) &&
                    solverSelectionButton_ &&
                    solverSelectionButton_->text() == QStringLiteral("openEMS") &&
                    solverMenu && solverMenu->actions().size() == 4 &&
                    solverMenu->actions().at(0)->text() ==
                        QStringLiteral("openEMS") &&
                    solverMenu->actions().at(1)->text() == QStringLiteral("MEEP") &&
                    solverMenu->actions().at(2)->text() ==
                        QStringLiteral("PALACE") &&
                    solverMenu->actions().at(3)->text() == QStringLiteral("NEC2") &&
                    domainSelectionButton_ &&
                    domainSelectionButton_->text() ==
                        QStringLiteral("Time Domain") &&
                    !domainSelectionButton_->icon().isNull() &&
                    domainMenu && domainMenu->actions().size() == 2 &&
                    domainMenu->actions().at(0)->text() ==
                        QStringLiteral("Frequency Domain Solver") &&
                    !domainMenu->actions().at(0)->icon().isNull() &&
                    domainMenu->actions().at(1)->text() ==
                        QStringLiteral("Time Domain Solver") &&
                    !domainMenu->actions().at(1)->icon().isNull() &&
                    consistentRibbonWidths && hasSolverOptions &&
                    solverOptionsDoesNothing
                ? 0
                : 1;
        if (!loaded) log_->appendPlainText(error);
        QApplication::quit();
    }

    void startOpenProjectSimulationSmokeTest(const QString& path,
                                             const QString& output) {
        smokeTestMode_ = true;
        QString error;
        const bool loaded = !path.isEmpty() && loadProjectFile(path, &error);
        if (!loaded || objects_.empty() || !validPortCoordinates()) {
            smokeTestExitCode_ = 1;
            if (!error.isEmpty()) log_->appendPlainText(error);
            QApplication::quit();
            return;
        }
        runSimulation(output);
    }

    void startCancelSmokeTest() {
        smokeTestMode_ = true;
        smokeTestExpectedCancel_ = true;
        ensureSmokeGeometry();
        maxTimesteps_->setValue(10000000);
        runSimulation(QString::fromUtf8(RF_SIM_SMOKE_OUTPUT));
        QTimer::singleShot(150, this, [this] {
            if (solverProcess_->state() != QProcess::NotRunning) {
                cancelSimulation();
            }
        });
    }

    int smokeTestExitCode() const { return smokeTestExitCode_; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        if (isFullScreen()) return;
        QTimer::singleShot(0, this, [this] {
            if (isFullScreen() || isMaximized()) return;
            QScreen* screen = windowHandle() ? windowHandle()->screen()
                                             : QApplication::primaryScreen();
            if (!screen) return;
            const QRect available = screen->availableGeometry();
            const QRect frame = frameGeometry();
            const int extraWidth = frame.width() - width();
            const int extraHeight = frame.height() - height();
            const int maxWidth = std::max(1, available.width() - extraWidth - 4);
            const int maxHeight = std::max(1, available.height() - extraHeight - 4);
            const int boundedWidth = std::min(width(), maxWidth);
            const int boundedHeight = std::min(height(), maxHeight);
            if (boundedWidth != width() || boundedHeight != height()) {
                resize(boundedWidth, boundedHeight);
                const QRect resizedFrame = frameGeometry();
                const int maxX = available.right() - resizedFrame.width() + 1;
                const int maxY = available.bottom() - resizedFrame.height() + 1;
                move(std::clamp(x(), available.left(), maxX),
                     std::clamp(y(), available.top(), maxY));
            }
        });
    }

    void closeEvent(QCloseEvent* event) override {
        QSettings settings(QStringLiteral("RFSim"), QStringLiteral("EMWorkbench"));
        settings.setValue(QStringLiteral("windowState"), saveState(5));
        settings.setValue(QStringLiteral("editorSplitter"), editorSplitter_->saveState());
        QMainWindow::closeEvent(event);
    }

private:
    void resetLayout() {
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);
        navigationDock_->show();
        resizeDocks({navigationDock_}, {250}, Qt::Horizontal);
        analysisPanelAction_->setChecked(true);
        contentSplitter_->setSizes({400, 320});
        editorSplitter_->setSizes({320, 780});
        if (auto* editor = workspace_->widget(0)) workspace_->setCurrentWidget(editor);
    }

    void createRibbonAndMenus(QPushButton* addPlate, QPushButton* addBox,
                              QPushButton* addCylinder, QPushButton* drawCurve,
                              QPushButton* exportStep,
                              QPushButton* openResults, QPushButton* drawPlate,
                              QPushButton* drawBox, QPushButton* topView,
                              QPushButton* newProject, QPushButton* openProject,
                              QPushButton* saveProject, QPushButton* duplicate,
                              QPushButton* importStep, QPushButton* fuse,
                              QPushButton* cut, QPushButton* grid,
                              QPushButton* fitView) {
        auto* ribbon = new QTabWidget;
        ribbon->setObjectName(QStringLiteral("mainRibbon"));
        ribbon->setMaximumHeight(138);
        const int ribbonButtonWidth =
            std::max(fontMetrics().horizontalAdvance(QStringLiteral("Open Project")),
                     fontMetrics().horizontalAdvance(QStringLiteral("Solver Options"))) +
            24;
        const QSize ribbonButtonSize(ribbonButtonWidth, 76);
        ribbon->findChild<QTabBar*>()->setExpanding(false);
        auto* ribbonStyle = new LeftAlignedTabStyle;
        ribbonStyle->setParent(ribbon);
        ribbon->setStyle(ribbonStyle);
        const auto iconFor = [this](const QString& label) {
            const QString text = label.toLower();
            if (text == QStringLiteral("run") ||
                text == QStringLiteral("run cad")) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#2e9e49")));
                QPainterPath play;
                play.moveTo(9, 5);
                play.lineTo(27, 16);
                play.lineTo(9, 27);
                play.closeSubpath();
                painter.drawPath(play);
                return QIcon(pixmap);
            }
            if (text.contains(QStringLiteral("time domain"))) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(QColor(QStringLiteral("#2474b5")), 2.4,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath wave;
                for (int point = 0; point <= 24; ++point) {
                    const double x = 4.0 + point;
                    const double phase = point * 2.0 * 3.14159265358979323846 / 24.0;
                    const double y = 16.0 - 8.0 * std::sin(phase);
                    if (point == 0) wave.moveTo(x, y);
                    else wave.lineTo(x, y);
                }
                painter.drawPath(wave);
                return QIcon(pixmap);
            }
            if (text.contains(QStringLiteral("frequency domain"))) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(QColor(QStringLiteral("#8054b8")), 2.0,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawLine(QPointF(5, 27), QPointF(28, 27));
                painter.drawLine(QPointF(5, 27), QPointF(5, 5));
                QPainterPath spectrum;
                spectrum.moveTo(6, 25);
                spectrum.lineTo(10, 23);
                spectrum.lineTo(13, 14);
                spectrum.lineTo(16, 23);
                spectrum.lineTo(20, 21);
                spectrum.lineTo(23, 8);
                spectrum.lineTo(26, 22);
                spectrum.lineTo(28, 24);
                painter.drawPath(spectrum);
                return QIcon(pixmap);
            }
            if (text == QStringLiteral("project settings") ||
                text == QStringLiteral("settings")) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette().color(QPalette::ButtonText));
                QPainterPath gear;
                constexpr double pi = 3.14159265358979323846;
                for (int tooth = 0; tooth < 8; ++tooth) {
                    for (int corner = 0; corner < 4; ++corner) {
                        const double angle =
                            (tooth * 4 + corner) * pi / 16.0 - pi / 2.0;
                        const double radius = (corner == 1 || corner == 2)
                                                  ? 14.0
                                                  : 10.8;
                        const QPointF point(
                            16.0 + radius * std::cos(angle),
                            16.0 + radius * std::sin(angle));
                        if (tooth == 0 && corner == 0) gear.moveTo(point);
                        else gear.lineTo(point);
                    }
                }
                gear.closeSubpath();
                painter.drawPath(gear);
                painter.setCompositionMode(QPainter::CompositionMode_Clear);
                painter.drawEllipse(QPointF(16.0, 16.0), 4.5, 4.5);
                return QIcon(pixmap);
            }
            if (text == QStringLiteral("close") ||
                text.contains(QStringLiteral("close project"))) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(QColor(QStringLiteral("#d32f2f")), 3.2,
                                    Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(8, 8), QPointF(24, 24));
                painter.drawLine(QPointF(24, 8), QPointF(8, 24));
                return QIcon(pixmap);
            }
            if (text == QStringLiteral("exit")) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(QStringLiteral("#d32f2f")), 2.8,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath exit;
                exit.moveTo(18, 5);
                exit.lineTo(9, 5);
                exit.lineTo(9, 27);
                exit.lineTo(18, 27);
                exit.moveTo(13, 16);
                exit.lineTo(27, 16);
                exit.moveTo(21, 10);
                exit.lineTo(27, 16);
                exit.lineTo(21, 22);
                painter.drawPath(exit);
                return QIcon(pixmap);
            }
            if (text.contains(QStringLiteral("import"))) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(palette().color(QPalette::ButtonText), 3.2,
                                    Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(16, 6), QPointF(16, 26));
                painter.drawLine(QPointF(6, 16), QPointF(26, 16));
                return QIcon(pixmap);
            }
            if (text.contains(QStringLiteral("export"))) {
                QPixmap pixmap(32, 32);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(palette().color(QPalette::ButtonText), 2.6,
                                    Qt::SolidLine, Qt::SquareCap, Qt::RoundJoin));
                QPainterPath exportPath;
                exportPath.moveTo(18, 6);
                exportPath.lineTo(6, 6);
                exportPath.lineTo(6, 26);
                exportPath.lineTo(26, 26);
                exportPath.lineTo(26, 16);
                exportPath.moveTo(13, 19);
                exportPath.lineTo(27, 5);
                exportPath.moveTo(19, 5);
                exportPath.lineTo(27, 5);
                exportPath.lineTo(27, 13);
                painter.drawPath(exportPath);
                return QIcon(pixmap);
            }
            QStyle::StandardPixmap icon = QStyle::SP_MessageBoxInformation;
            if (text.contains(QStringLiteral("new")) ||
                text.contains(QStringLiteral("duplicate")) ||
                text.contains(QStringLiteral("add")) ||
                text.contains(QStringLiteral("draw"))) {
                icon = QStyle::SP_FileDialogNewFolder;
            } else if (text.contains(QStringLiteral("open")) ||
                       text.contains(QStringLiteral("import"))) {
                icon = QStyle::SP_DirOpenIcon;
            } else if (text.contains(QStringLiteral("save"))) {
                icon = QStyle::SP_DialogSaveButton;
            } else if (text.contains(QStringLiteral("run"))) {
                icon = QStyle::SP_MediaPlay;
            } else if (text.contains(QStringLiteral("cancel"))) {
                icon = QStyle::SP_MediaStop;
            } else if (text.contains(QStringLiteral("fuse")) ||
                       text.contains(QStringLiteral("apply"))) {
                icon = QStyle::SP_DialogApplyButton;
            } else if (text.contains(QStringLiteral("cut")) ||
                       text.contains(QStringLiteral("remove"))) {
                icon = QStyle::SP_TrashIcon;
            } else if (text.contains(QStringLiteral("fit")) ||
                       text.contains(QStringLiteral("top")) ||
                       text.contains(QStringLiteral("view"))) {
                icon = QStyle::SP_ArrowUp;
            } else if (text.contains(QStringLiteral("grid"))) {
                icon = QStyle::SP_FileDialogDetailedView;
            } else if (text.contains(QStringLiteral("result")) ||
                       text.contains(QStringLiteral("plot")) ||
                       text.contains(QStringLiteral("parameter")) ||
                       text.contains(QStringLiteral("s-params"))) {
                icon = QStyle::SP_FileDialogContentsView;
            } else if (text.contains(QStringLiteral("solver"))) {
                icon = QStyle::SP_ComputerIcon;
            } else if (text.contains(QStringLiteral("help"))) {
                icon = QStyle::SP_DialogHelpButton;
            }
            return style()->standardIcon(icon);
        };
        const auto tooltipFor = [](const QString& label) {
            const QString text = label.toLower();
            if (text == QStringLiteral("new") ||
                text == QStringLiteral("new project"))
                return QStringLiteral("Tạo dự án mô phỏng mới.");
            if (text == QStringLiteral("open project"))
                return QStringLiteral("Mở dự án đã tồn tại.");
            if (text == QStringLiteral("open recent"))
                return QStringLiteral("Hiển thị danh sách dự án mở gần đây.");
            if (text == QStringLiteral("save"))
                return QStringLiteral("Lưu dự án hiện tại.");
            if (text == QStringLiteral("save as"))
                return QStringLiteral("Lưu dự án với tên hoặc vị trí mới.");
            if (text == QStringLiteral("save all"))
                return QStringLiteral("Lưu toàn bộ tài liệu và dữ liệu đang mở.");
            if (text == QStringLiteral("import"))
                return QStringLiteral("Nhập mô hình CAD và dữ liệu hình học được hỗ trợ.");
            if (text == QStringLiteral("export"))
                return QStringLiteral("Xuất mô hình CAD hoặc dự án mô phỏng.");
            if (text == QStringLiteral("settings") ||
                text == QStringLiteral("project settings"))
                return QStringLiteral("Thiết lập cấu hình mô phỏng của dự án.");
            if (text == QStringLiteral("close") ||
                text == QStringLiteral("close project"))
                return QStringLiteral("Đóng dự án hiện tại.");
            if (text == QStringLiteral("exit"))
                return QStringLiteral("Thoát phần mềm.");
            if (text.contains(QStringLiteral("draw box")))
                return QStringLiteral("Drag a rectangular footprint in Top View to create a box.");
            if (text.contains(QStringLiteral("draw plate")))
                return QStringLiteral("Drag a rectangular footprint in Top View to create a plate.");
            if (text.contains(QStringLiteral("import step")))
                return QStringLiteral("Import a validated STEP solid.");
            if (text.contains(QStringLiteral("fuse")))
                return QStringLiteral("Union exactly two selected solids.");
            if (text == QStringLiteral("cut"))
                return QStringLiteral("Subtract the second selected solid from the first.");
            if (text.contains(QStringLiteral("run cad")))
                return QStringLiteral("Run openEMS on the current CAD scene and lumped port.");
            if (text == QStringLiteral("run"))
                return QStringLiteral("Chạy mô phỏng CAD hiện tại.");
            if (text == QStringLiteral("solver options"))
                return QStringLiteral("Tùy chọn solver sẽ được bổ sung sau.");
            return label;
        };
        const auto makeRibbonPage = [this, ribbon, saveProject, ribbonButtonSize,
                                      &iconFor, &tooltipFor](
                                        const QString& name,
                                        std::initializer_list<
                                            QPair<QString, std::function<void()>>> actions) {
            auto* page = new QWidget;
            auto* layout = new QHBoxLayout(page);
            layout->setAlignment(Qt::AlignLeft);
            layout->setContentsMargins(8, 6, 8, 6);
            const auto makeMenuArrowButton =
                [](QWidget* parent, const QString& accessibleName,
                   const QString& tooltip,
                   const std::function<void()>& callback) {
                    auto* arrowButton = new QToolButton(parent);
                    QPixmap arrowPixmap(12, 12);
                    arrowPixmap.fill(Qt::transparent);
                    QPainter arrowPainter(&arrowPixmap);
                    arrowPainter.setRenderHint(QPainter::Antialiasing);
                    arrowPainter.setPen(Qt::NoPen);
                    arrowPainter.setBrush(
                        arrowButton->palette().color(QPalette::ButtonText));
                    QPainterPath arrowPath;
                    arrowPath.moveTo(1, 3);
                    arrowPath.lineTo(11, 3);
                    arrowPath.lineTo(6, 9);
                    arrowPath.closeSubpath();
                    arrowPainter.drawPath(arrowPath);
                    arrowButton->setIcon(QIcon(arrowPixmap));
                    arrowButton->setIconSize(QSize(16, 16));
                    arrowButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    arrowButton->setStyleSheet(
                        QStringLiteral("QToolButton { padding: 0px; }"));
                    arrowButton->setAccessibleName(accessibleName);
                    arrowButton->setToolTip(tooltip);
                    arrowButton->setFixedSize(22, 76);
                    arrowButton->setAutoRaise(true);
                    QObject::connect(
                        arrowButton, &QToolButton::clicked, arrowButton,
                        [callback] { callback(); });
                    return arrowButton;
                };
            const auto addMenuChoice =
                [ribbonButtonSize](QMenu* menu, const QString& actionText,
                                   const QString& buttonText, const QIcon& icon,
                                   const QString& tooltip,
                                   const std::function<void()>& callback) {
                    auto* action = new QWidgetAction(menu);
                    action->setText(actionText);
                    action->setIcon(icon);
                    auto* optionButton = new QToolButton;
                    optionButton->setText(buttonText);
                    optionButton->setIcon(icon);
                    optionButton->setIconSize(QSize(26, 26));
                    optionButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    optionButton->setToolTip(tooltip);
                    optionButton->setFixedSize(ribbonButtonSize);
                    optionButton->setAccessibleName(actionText);
                    optionButton->setAutoRaise(true);
                    QObject::connect(optionButton, &QToolButton::clicked,
                                     optionButton, [menu, callback] {
                                         callback();
                                         menu->close();
                                     });
                    action->setDefaultWidget(optionButton);
                    menu->addAction(action);
                };
            if (name == QStringLiteral("Simulation")) {
                auto* solverGroup = new QWidget(page);
                auto* solverLayout = new QHBoxLayout(solverGroup);
                solverLayout->setContentsMargins(0, 0, 0, 0);
                solverLayout->setSpacing(0);
                solverSelectionButton_ = new QToolButton(solverGroup);
                solverSelectionButton_->setObjectName(
                    QStringLiteral("solverSelection"));
                solverSelectionButton_->setText(QStringLiteral("openEMS"));
                solverSelectionButton_->setIcon(
                    iconFor(QStringLiteral("Solver Selection")));
                solverSelectionButton_->setIconSize(QSize(26, 26));
                solverSelectionButton_->setToolButtonStyle(
                    Qt::ToolButtonTextUnderIcon);
                solverSelectionButton_->setPopupMode(
                    QToolButton::InstantPopup);
                solverSelectionButton_->setFixedSize(ribbonButtonSize);
                solverSelectionButton_->setAccessibleName(
                    QStringLiteral("Solver Selection"));
                auto* solverMenu = new QMenu(solverSelectionButton_);
                solverMenu->setObjectName(QStringLiteral("solverMenu"));
                addMenuChoice(
                    solverMenu, QStringLiteral("openEMS"),
                    QStringLiteral("openEMS"),
                    iconFor(QStringLiteral("Solver Selection")),
                    QStringLiteral("Chọn solver openEMS."),
                    [this] {
                        solverSelectionButton_->setText(QStringLiteral("openEMS"));
                    });
                addMenuChoice(
                    solverMenu, QStringLiteral("MEEP"), QStringLiteral("MEEP"),
                    iconFor(QStringLiteral("Solver Selection")),
                    QStringLiteral("Chọn solver MEEP."),
                    [this] {
                        solverSelectionButton_->setText(QStringLiteral("MEEP"));
                    });
                addMenuChoice(
                    solverMenu, QStringLiteral("PALACE"),
                    QStringLiteral("PALACE"),
                    iconFor(QStringLiteral("Solver Selection")),
                    QStringLiteral("Chọn solver PALACE."),
                    [this] {
                        solverSelectionButton_->setText(QStringLiteral("PALACE"));
                    });
                addMenuChoice(
                    solverMenu, QStringLiteral("NEC2"), QStringLiteral("NEC2"),
                    iconFor(QStringLiteral("Solver Selection")),
                    QStringLiteral("Chọn solver NEC2."),
                    [this] {
                        solverSelectionButton_->setText(QStringLiteral("NEC2"));
                    });
                solverSelectionButton_->setToolTip(
                    QStringLiteral("Solver Selection — openEMS."));
                const auto showSolverMenu = [solverMenu, this] {
                    const int horizontalOffset =
                        (solverSelectionButton_->width() -
                         solverMenu->sizeHint().width()) / 2;
                    solverMenu->popup(solverSelectionButton_->mapToGlobal(
                        QPoint(horizontalOffset, solverSelectionButton_->height())));
                };
                QObject::connect(solverSelectionButton_, &QToolButton::clicked,
                                 solverSelectionButton_, showSolverMenu);
                solverLayout->addWidget(solverSelectionButton_);
                solverLayout->addWidget(makeMenuArrowButton(
                    solverGroup, QStringLiteral("Solver Selection options"),
                    QStringLiteral("Select a solver."),
                    showSolverMenu));
                layout->addWidget(solverGroup);

                auto* domainGroup = new QWidget(page);
                auto* domainLayout = new QHBoxLayout(domainGroup);
                domainLayout->setContentsMargins(0, 0, 0, 0);
                domainLayout->setSpacing(0);
                domainSelectionButton_ = new QToolButton(domainGroup);
                domainSelectionButton_->setObjectName(
                    QStringLiteral("domainSelection"));
                domainSelectionButton_->setText(QStringLiteral("Time Domain"));
                const QIcon timeDomainIcon =
                    iconFor(QStringLiteral("Time Domain Solver"));
                const QIcon frequencyDomainIcon =
                    iconFor(QStringLiteral("Frequency Domain Solver"));
                domainSelectionButton_->setIcon(timeDomainIcon);
                domainSelectionButton_->setIconSize(QSize(26, 26));
                domainSelectionButton_->setToolButtonStyle(
                    Qt::ToolButtonTextUnderIcon);
                domainSelectionButton_->setFixedSize(ribbonButtonSize);
                domainSelectionButton_->setAccessibleName(
                    QStringLiteral("Domain Selection"));
                domainSelectionButton_->setToolTip(
                    QStringLiteral("Domain — Time Domain Solver. "
                                   "Giải bài toán trong miền thời gian."));
                auto* domainMenu = new QMenu(domainSelectionButton_);
                domainMenu->setObjectName(QStringLiteral("domainMenu"));
                addMenuChoice(
                    domainMenu, QStringLiteral("Frequency Domain Solver"),
                    QStringLiteral("Freq. Domain"), frequencyDomainIcon,
                    QStringLiteral("Frequency Domain Solver — "
                                   "Giải bài toán trong miền tần số."),
                    [this, frequencyDomainIcon] {
                        domainSelectionButton_->setText(
                            QStringLiteral("Freq. Domain"));
                        domainSelectionButton_->setIcon(frequencyDomainIcon);
                        domainSelectionButton_->setToolTip(
                            QStringLiteral("Domain — Frequency Domain Solver. "
                                           "Giải bài toán trong miền tần số."));
                    });
                addMenuChoice(
                    domainMenu, QStringLiteral("Time Domain Solver"),
                    QStringLiteral("Time Domain"), timeDomainIcon,
                    QStringLiteral("Time Domain Solver — "
                                   "Giải bài toán trong miền thời gian."),
                    [this, timeDomainIcon] {
                        domainSelectionButton_->setText(
                            QStringLiteral("Time Domain"));
                        domainSelectionButton_->setIcon(timeDomainIcon);
                        domainSelectionButton_->setToolTip(
                            QStringLiteral("Domain — Time Domain Solver. "
                                           "Giải bài toán trong miền thời gian."));
                    });
                const auto showDomainMenu = [domainMenu, this] {
                    const int horizontalOffset =
                        (domainSelectionButton_->width() -
                         domainMenu->sizeHint().width()) / 2;
                    domainMenu->popup(domainSelectionButton_->mapToGlobal(
                        QPoint(horizontalOffset, domainSelectionButton_->height())));
                };
                QObject::connect(domainSelectionButton_, &QToolButton::clicked,
                                 domainSelectionButton_, showDomainMenu);
                domainLayout->addWidget(domainSelectionButton_);
                domainLayout->addWidget(makeMenuArrowButton(
                    domainGroup, QStringLiteral("Domain options"),
                    QStringLiteral("Select a simulation domain."),
                    showDomainMenu));
                layout->addWidget(domainGroup);
            }
            for (const auto& entry : actions) {
                if (name == QStringLiteral("File") &&
                    entry.first == QStringLiteral("Open Project")) {
                    auto* openGroup = new QWidget(page);
                    auto* openLayout = new QHBoxLayout(openGroup);
                    openLayout->setContentsMargins(0, 0, 0, 0);
                    openLayout->setSpacing(0);

                    auto* openButton = new QToolButton(openGroup);
                    openButton->setText(entry.first);
                    openButton->setIcon(iconFor(entry.first));
                    openButton->setIconSize(QSize(26, 26));
                    openButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    openButton->setToolTip(tooltipFor(entry.first));
                    openButton->setStatusTip(tooltipFor(entry.first));
                    openButton->setFixedSize(ribbonButtonSize);
                    openButton->setAccessibleName(entry.first);
                    openButton->setAutoRaise(true);
                    QObject::connect(openButton, &QToolButton::clicked,
                                     openButton, entry.second);
                    openLayout->addWidget(openButton);
                    openLayout->addWidget(makeMenuArrowButton(
                        openGroup, QStringLiteral("Open recent projects"),
                        QStringLiteral("Open a recently used project."),
                        [this, openButton] { showRecentProjects(openButton); }));
                    layout->addWidget(openGroup);
                    continue;
                }

                if (name == QStringLiteral("File") &&
                    entry.first == QStringLiteral("Save")) {
                    auto* saveGroup = new QWidget(page);
                    auto* saveLayout = new QHBoxLayout(saveGroup);
                    saveLayout->setContentsMargins(0, 0, 0, 0);
                    saveLayout->setSpacing(0);

                    auto* saveButton = new QToolButton(saveGroup);
                    saveButton->setText(entry.first);
                    saveButton->setIcon(iconFor(entry.first));
                    saveButton->setIconSize(QSize(26, 26));
                    saveButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    saveButton->setToolTip(tooltipFor(entry.first));
                    saveButton->setStatusTip(tooltipFor(entry.first));
                    saveButton->setFixedSize(ribbonButtonSize);
                    saveButton->setAccessibleName(entry.first);
                    saveButton->setAutoRaise(true);
                    QObject::connect(saveButton, &QToolButton::clicked,
                                     saveButton, entry.second);
                    saveLayout->addWidget(saveButton);

                    auto* saveMenu = new QMenu(saveGroup);
                    const auto addSaveOption =
                        [saveMenu, ribbonButtonSize](const QString& text,
                                   QStyle::StandardPixmap icon,
                                   const QString& tooltip,
                                   const std::function<void()>& callback) {
                            auto* action = new QWidgetAction(saveMenu);
                            auto* optionButton = new QToolButton;
                            optionButton->setText(text);
                            optionButton->setIcon(
                                QApplication::style()->standardIcon(icon));
                            optionButton->setIconSize(QSize(26, 26));
                            optionButton->setToolButtonStyle(
                                Qt::ToolButtonTextUnderIcon);
                            optionButton->setToolTip(tooltip);
                            optionButton->setFixedSize(ribbonButtonSize);
                            optionButton->setAccessibleName(text);
                            optionButton->setAutoRaise(true);
                            QObject::connect(
                                optionButton, &QToolButton::clicked,
                                optionButton, [saveMenu, callback] {
                                    callback();
                                    saveMenu->close();
                                });
                            action->setDefaultWidget(optionButton);
                            saveMenu->addAction(action);
                        };
                    addSaveOption(
                        QStringLiteral("Save As"), QStyle::SP_FileIcon,
                        QStringLiteral("Save the project with a new name or location."),
                        [this] { saveProjectAs(); });
                    addSaveOption(
                        QStringLiteral("Save All"), QStyle::SP_DriveHDIcon,
                        QStringLiteral("Save all open project data."),
                        [saveProject] { saveProject->click(); });
                    auto* saveMenuButton = makeMenuArrowButton(
                        saveGroup, QStringLiteral("Save options"),
                        QStringLiteral("Save As and Save All"),
                        [saveMenu, saveButton] {
                            const int horizontalOffset =
                                (saveButton->width() - saveMenu->sizeHint().width()) / 2;
                            const QPoint popupPosition = saveButton->mapToGlobal(
                                QPoint(horizontalOffset, saveButton->height()));
                            saveMenu->popup(popupPosition);
                        });
                    saveLayout->addWidget(saveMenuButton);
                    layout->addWidget(saveGroup);
                    continue;
                }

                auto* button = new QToolButton;
                button->setText(entry.first);
                button->setIcon(iconFor(entry.first));
                button->setIconSize(QSize(26, 26));
                button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                button->setToolTip(tooltipFor(entry.first));
                button->setStatusTip(tooltipFor(entry.first));
                button->setFixedSize(ribbonButtonSize);
                button->setAccessibleName(entry.first);
                button->setAutoRaise(true);
                layout->addWidget(button);
                QObject::connect(button, &QToolButton::clicked, button, entry.second);
            }
            layout->addStretch();
            auto* scroll = new QScrollArea;
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scroll->setWidget(page);
            ribbon->addTab(scroll, name);
        };
        const auto showImportMenu = [this, importStep] {
            QMenu menu(this);
            auto* stepAction =
                menu.addAction(QStringLiteral("Import CAD model (STEP)…"));
            if (menu.exec(QCursor::pos()) == stepAction) importStep->click();
        };
        const auto showExportMenu = [this, exportStep] {
            QMenu menu(this);
            auto* stepAction =
                menu.addAction(QStringLiteral("Export CAD model as STEP…"));
            auto* projectAction =
                menu.addAction(QStringLiteral("Export OEM project…"));
            QAction* selected = menu.exec(QCursor::pos());
            if (selected == stepAction) exportStep->click();
            else if (selected == projectAction) saveProjectAs();
        };
        makeRibbonPage(QStringLiteral("File"), {
            {QStringLiteral("New"), [newProject] { newProject->click(); }},
            {QStringLiteral("Open Project"), [openProject] { openProject->click(); }},
            {QStringLiteral("Save"), [saveProject] { saveProject->click(); }},
            {QStringLiteral("Import"), showImportMenu},
            {QStringLiteral("Export"), showExportMenu},
            {QStringLiteral("Settings"), [this] {
                workspace_->setCurrentWidget(workspace_->widget(2));
            }},
            {QStringLiteral("Close"), [this] { closeProject(); }},
            {QStringLiteral("Exit"), [this] { close(); }}
        });
        makeRibbonPage(QStringLiteral("Modeling"), {
            {QStringLiteral("Add Plate"), [addPlate] { addPlate->click(); }},
            {QStringLiteral("Add Box"), [addBox] { addBox->click(); }},
            {QStringLiteral("Add Cylinder"), [addCylinder] { addCylinder->click(); }},
            {QStringLiteral("Draw Curve"), [drawCurve] { drawCurve->click(); }},
            {QStringLiteral("Draw Plate"), [drawPlate] { drawPlate->click(); }},
            {QStringLiteral("Draw Box"), [drawBox] { drawBox->click(); }},
            {QStringLiteral("3D View"), [topView] { topView->click(); }},
            {QStringLiteral("Import STEP"), [importStep] { importStep->click(); }},
            {QStringLiteral("Fuse"), [fuse] { fuse->click(); }},
            {QStringLiteral("Cut"), [cut] { cut->click(); }},
            {QStringLiteral("Duplicate"), [duplicate] { duplicate->click(); }},
            {QStringLiteral("Grid"), [grid] { grid->click(); }},
            {QStringLiteral("Fit View"), [fitView] { fitView->click(); }},
            {QStringLiteral("Export STEP"), [exportStep] { exportStep->click(); }}
        });
        makeRibbonPage(QStringLiteral("Simulation"), {
            {QStringLiteral("Run"), [this] { runButton_->click(); }},
            {QStringLiteral("Solver Options"), [] {}}
        });
        makeRibbonPage(QStringLiteral("Results"), {
            {QStringLiteral("S-Params"), [this] {
                workspace_->setCurrentWidget(resultsPage_);
                resultPlots_->setCurrentWidget(s11Plot_);
            }},
            {QStringLiteral("VSWR"), [this] {
                workspace_->setCurrentWidget(resultsPage_);
                resultPlots_->setCurrentWidget(vswrPlot_);
            }},
            {QStringLiteral("Waveform"), [this] {
                workspace_->setCurrentWidget(resultsPage_);
                resultPlots_->setCurrentWidget(wavePlot_);
            }},
            {QStringLiteral("Results Folder"), [openResults] { openResults->click(); }}
        });
        makeRibbonPage(QStringLiteral("View"), {
            {QStringLiteral("3D View"), [topView] { topView->click(); }},
            {QStringLiteral("Grid"), [grid] { grid->click(); }},
            {QStringLiteral("Fit Camera"), [fitView] { fitView->click(); }},
            {QStringLiteral("Navigation"), [this] {
                if (navigationDock_) navigationDock_->toggleViewAction()->trigger();
            }},
            {QStringLiteral("Parameters"), [this] {
                if (analysisPanelAction_) analysisPanelAction_->trigger();
            }}
        });

        auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
        auto* newAction = fileMenu->addAction(iconFor(QStringLiteral("New Project")),
                                              QStringLiteral("New Project"));
        connect(newAction, &QAction::triggered, newProject, &QPushButton::click);
        auto* openAction = fileMenu->addAction(iconFor(QStringLiteral("Open Project")),
                                               QStringLiteral("Open Project…"));
        connect(openAction, &QAction::triggered, openProject, &QPushButton::click);
        auto* recentAction = fileMenu->addAction(
            iconFor(QStringLiteral("Open Recent")), QStringLiteral("Open Recent"));
        connect(recentAction, &QAction::triggered, this,
                [this] { showRecentProjects(); });
        auto* saveAction = fileMenu->addAction(iconFor(QStringLiteral("Save")),
                                               QStringLiteral("Save"));
        connect(saveAction, &QAction::triggered, saveProject, &QPushButton::click);
        fileMenu->addAction(iconFor(QStringLiteral("Save As")),
                            QStringLiteral("Save As…"), this,
                            [this] { saveProjectAs(); });
        fileMenu->addAction(iconFor(QStringLiteral("Save All")),
                            QStringLiteral("Save All"), saveProject,
                            &QPushButton::click);
        fileMenu->addSeparator();
        fileMenu->addAction(iconFor(QStringLiteral("Import")),
                            QStringLiteral("Import…"), this, showImportMenu);
        fileMenu->addAction(iconFor(QStringLiteral("Export")),
                            QStringLiteral("Export…"), this, showExportMenu);
        fileMenu->addAction(iconFor(QStringLiteral("Project Settings")),
                            QStringLiteral("Project Settings"), this, [this] {
            workspace_->setCurrentWidget(workspace_->widget(2));
        });
        fileMenu->addAction(iconFor(QStringLiteral("Close Project")),
                            QStringLiteral("Close Project"), this,
                            [this] { closeProject(); });
        fileMenu->addSeparator();
        fileMenu->addAction(iconFor(QStringLiteral("Exit")),
                            QStringLiteral("Exit"), this, &QWidget::close);
        auto* homeMenu = menuBar()->addMenu(QStringLiteral("Home"));
        auto* runCadAction = homeMenu->addAction(iconFor(QStringLiteral("Run CAD")),
                                                 QStringLiteral("Run CAD simulation"));
        connect(runCadAction, &QAction::triggered, runButton_, &QPushButton::click);
        auto* modelingMenu = menuBar()->addMenu(QStringLiteral("Modeling"));
        auto* addPlateAction = modelingMenu->addAction(iconFor(QStringLiteral("Add Plate")),
                                                       QStringLiteral("Add plate"));
        connect(addPlateAction, &QAction::triggered, addPlate, &QPushButton::click);
        auto* addBoxAction = modelingMenu->addAction(iconFor(QStringLiteral("Add Box")),
                                                     QStringLiteral("Add box"));
        connect(addBoxAction, &QAction::triggered, addBox, &QPushButton::click);
        auto* addCylinderAction = modelingMenu->addAction(
            iconFor(QStringLiteral("Add Cylinder")), QStringLiteral("Add cylinder"));
        connect(addCylinderAction, &QAction::triggered, addCylinder, &QPushButton::click);
        auto* fuseAction = modelingMenu->addAction(iconFor(QStringLiteral("Fuse")),
                                                   QStringLiteral("Boolean Fuse selected"));
        connect(fuseAction, &QAction::triggered, fuse, &QPushButton::click);
        auto* cutAction = modelingMenu->addAction(iconFor(QStringLiteral("Cut")),
                                                  QStringLiteral("Boolean Cut first minus second"));
        connect(cutAction, &QAction::triggered, cut, &QPushButton::click);
        auto* simulationMenu = menuBar()->addMenu(QStringLiteral("Simulation"));
        auto* solverAction = simulationMenu->addAction(iconFor(QStringLiteral("Run CAD")),
                                                       QStringLiteral("Run CAD solver"));
        connect(solverAction, &QAction::triggered, runButton_, &QPushButton::click);
        auto* postMenu = menuBar()->addMenu(QStringLiteral("Post-Processing"));
        postMenu->addAction(iconFor(QStringLiteral("Results")),
                            QStringLiteral("Show Results"), this, [this] {
            workspace_->setCurrentWidget(resultsPage_);
        });
        auto* openOutputAction = postMenu->addAction(
            iconFor(QStringLiteral("Open Results")), QStringLiteral("Open Results Folder"));
        connect(openOutputAction, &QAction::triggered, openResults, &QPushButton::click);
        auto* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
        auto* gridAction = viewMenu->addAction(iconFor(QStringLiteral("Grid")),
                                               QStringLiteral("Reference Grid"));
        gridAction->setCheckable(true);
        gridAction->setChecked(true);
        connect(grid, &QPushButton::toggled, gridAction,
                [gridAction](bool enabled) { gridAction->setChecked(enabled); });
        connect(gridAction, &QAction::toggled, this, [this, grid](bool enabled) {
            gridVisible_ = enabled;
            view_->setShowGrid(enabled);
            if (grid->isChecked() != enabled) grid->setChecked(enabled);
        });
        viewMenu->addAction(iconFor(QStringLiteral("Fit View")),
                            QStringLiteral("Fit Camera"), this, [this] {
            view_->resetCamera();
            topViewEnabled_ = false;
        });
        auto* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
        for (const QString& name : {
                 QStringLiteral("Parameter Sweep — not available"),
                 QStringLiteral("Optimization — not available"),
                 QStringLiteral("Circuit co-simulation — not available")}) {
            auto* unavailable = toolsMenu->addAction(name);
            unavailable->setEnabled(false);
            unavailable->setToolTip(QStringLiteral("Not implemented in this prototype."));
        }
        auto* helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
        helpMenu->addAction(iconFor(QStringLiteral("Help")),
                            QStringLiteral("Open User Guide"), this, [] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QString::fromUtf8(RF_SIM_INSTRUCTION_FILE)));
        });
        helpMenu->addAction(iconFor(QStringLiteral("About")),
                            QStringLiteral("About"), this, [this] {
            QMessageBox::information(
                this, QStringLiteral("About RF Simulation"),
                QStringLiteral("RF Simulation prototype\nQt + Open CASCADE + openEMS\n"
                               "Supports CAD geometry and lumped-port workflows."));
        });

        auto* shell = new QWidget;
        auto* layout = new QVBoxLayout(shell);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        contentSplitter_ = new QSplitter(Qt::Vertical);
        contentSplitter_->setObjectName(QStringLiteral("workspaceAnalysisSplitter"));
        contentSplitter_->setHandleWidth(6);
        contentSplitter_->setChildrenCollapsible(false);
        contentSplitter_->addWidget(workspace_);
        analysisPanel_ = new QWidget;
        auto* analysisLayout = new QVBoxLayout(analysisPanel_);
        analysisLayout->setContentsMargins(0, 0, 0, 0);
        analysisLayout->setSpacing(0);
        auto* analysisTabs = new QTabWidget;
        analysisTabs->findChild<QTabBar*>()->setExpanding(false);
        auto* analysisTabStyle = new LeftAlignedTabStyle;
        analysisTabStyle->setParent(analysisTabs);
        analysisTabs->setStyle(analysisTabStyle);
        analysisTabs->addTab(parameterTable_, QStringLiteral("Parameter List"));
        auto* messages = new QWidget;
        auto* messagesLayout = new QVBoxLayout(messages);
        messagesLayout->setContentsMargins(4, 4, 4, 4);
        messagesLayout->addWidget(progressBar_);
        messagesLayout->addWidget(log_, 1);
        analysisTabs->addTab(messages, QStringLiteral("Progress / Messages"));
        analysisLayout->addWidget(analysisTabs, 1);
        analysisPanel_->setStyleSheet(QStringLiteral(
            "QWidget { font-size: 9pt; }"
            "QTabBar::tab { padding: 4px 8px; }"
            "QHeaderView::section { font-size: 8pt; padding: 1px 4px; }"
            "QTableWidget, QPlainTextEdit { font-size: 9pt; }"
            "QTableWidget::item { padding: 0px 3px; }"));
        analysisPanel_->setMinimumHeight(190);
        contentSplitter_->addWidget(analysisPanel_);
        contentSplitter_->handle(1)->setCursor(Qt::SplitVCursor);
        contentSplitter_->setStretchFactor(0, 1);
        contentSplitter_->setStretchFactor(1, 2);
        contentSplitter_->setSizes({400, 320});
        layout->addWidget(contentSplitter_, 1);
        setCentralWidget(shell);
        auto* ribbonToolbar = new QToolBar(QStringLiteral("Ribbon"), this);
        ribbonToolbar->setObjectName(QStringLiteral("ribbonToolbar"));
        ribbonToolbar->setMovable(false);
        ribbonToolbar->setFloatable(false);
        ribbonToolbar->addWidget(ribbon);
        addToolBar(Qt::TopToolBarArea, ribbonToolbar);
        setDockNestingEnabled(true);

        navigationDock_ = new QDockWidget(QStringLiteral("Navigation Tree"), this);
        navigationDock_->setObjectName(QStringLiteral("navigationTreeDock"));
        navigationDock_->setAllowedAreas(Qt::LeftDockWidgetArea);
        navigationDock_->setFeatures(QDockWidget::DockWidgetMovable);
        navigationPanel_->setParent(nullptr);
        navigationDock_->setWidget(navigationPanel_);
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);

        viewMenu->addAction(navigationDock_->toggleViewAction());
        analysisPanelAction_ = viewMenu->addAction(
            QStringLiteral("Parameters / Messages"));
        analysisPanelAction_->setCheckable(true);
        analysisPanelAction_->setChecked(true);
        connect(analysisPanelAction_, &QAction::toggled, analysisPanel_,
                &QWidget::setVisible);
        auto* resetLayoutAction = viewMenu->addAction(iconFor(QStringLiteral("Reset Layout")),
                                                      QStringLiteral("Reset Layout"));
        connect(resetLayoutAction, &QAction::triggered, this, [this] { resetLayout(); });
        resizeDocks({navigationDock_}, {250}, Qt::Horizontal);

        QSettings settings(QStringLiteral("RFSim"), QStringLiteral("EMWorkbench"));
        bool restoredState = false;
        if (settings.contains(QStringLiteral("windowState"))) {
            restoredState = restoreState(
                settings.value(QStringLiteral("windowState")).toByteArray(), 5);
        }
        if (!restoredState) {
            resetLayout();
        }
        if (settings.contains(QStringLiteral("editorSplitter"))) {
            editorSplitter_->restoreState(
                settings.value(QStringLiteral("editorSplitter")).toByteArray());
        }
        statusBar()->showMessage(QStringLiteral("Ready — openEMS | Units: mm, GHz"));
        auto* unitStatus = new QLabel(QStringLiteral("mm   GHz   ns   deg"));
        statusBar()->addPermanentWidget(unitStatus);
        statusBar()->setStyleSheet(QStringLiteral(
            "QStatusBar { background: #e8edf4; color: #34445a; border-top: 1px solid #c8d1dd; }"));
        setStyleSheet(QStringLiteral(
            "QMainWindow { background: #edf1f6; }"
            "QSplitter#workspaceAnalysisSplitter::handle:vertical { background: #aab8c8; }"
            "QSplitter#workspaceAnalysisSplitter::handle:vertical:hover { background: #2474b5; }"
            "QTabWidget::pane { border: 1px solid #c8d1dd; background: #ffffff; }"
            "QTabBar::tab { background: #e1e7ef; padding: 7px 14px; }"
            "QTabBar::tab:selected { background: #ffffff; border-bottom: 2px solid #2474b5; }"
            "QTreeWidget { background: #ffffff; border: 1px solid #c8d1dd; }"
            "QPushButton, QToolButton { padding: 6px 9px; background: #f7f9fc; border: 1px solid #b8c5d4; border-radius: 3px; }"
            "QPushButton:hover, QToolButton:hover { background: #e7f1fa; border-color: #4a8cc2; }"
            "QDockWidget::title { background: #dce4ee; padding: 5px; }"));
        setProjectCanvasAppearance(false);
    }

    void beginRectangleDrawing(rfsim::SolidKind kind) {
        topViewEnabled_ = true;
        workspace_->setCurrentWidget(workspace_->widget(0));
        view_->setDrawMode([this, kind](double x, double y, double length,
                                        double width) {
            rfsim::SolidObject object;
            object.kind = kind;
            object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            object.featureType = kind == rfsim::SolidKind::PlateWithHole
                                     ? "primitive.plate-with-hole"
                                     : "primitive.box";
            object.name = (kind == rfsim::SolidKind::PlateWithHole ? "Plate " : "Box ") +
                          std::to_string(objects_.size() + 1);
            object.x = x;
            object.y = y;
            object.length = length;
            object.width = width;
            object.height = kind == rfsim::SolidKind::Box
                                ? std::max(height_->value(), 10.0)
                                : height_->value();
            if (kind == rfsim::SolidKind::PlateWithHole) {
                object.holeRadius = std::max(0.1, std::min(length, width) * 0.1);
            }
            recordGeometryFeature(object);
            objects_.push_back(object);
            const int row = static_cast<int>(objects_.size() - 1);
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Created %1 × %2 mm footprint.")
                                 .arg(length, 0, 'f', 1)
                                 .arg(width, 0, 'f', 1));
        });
        status_->setText(QStringLiteral(
            "Top view: drag across the viewport to draw the footprint; release to create the solid."));
        statusBar()->showMessage(QStringLiteral("Draw mode — drag a rectangle in the viewport"));
    }

    void beginCurveDrawing() {
        topViewEnabled_ = true;
        workspace_->setCurrentWidget(workspace_->widget(0));
        view_->setCurvePlaneZ(curvePlaneZ_->value());
        view_->setCurveDrawMode([this](const std::vector<rfsim::Point3D>& points) {
            if (points.size() < 2) return;
            const std::vector<rfsim::Point3D>& worldPoints = points;
            rfsim::SolidObject object;
            object.kind = rfsim::SolidKind::Curve;
            object.name = "Curve " + std::to_string(objects_.size() + 1);
            object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            object.featureType = "curve.polyline";
            double minX = worldPoints.front().x, maxX = minX;
            double minY = worldPoints.front().y, maxY = minY;
            double minZ = worldPoints.front().z, maxZ = minZ;
            for (const auto& point : worldPoints) {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
                minZ = std::min(minZ, point.z);
                maxZ = std::max(maxZ, point.z);
            }
            object.x = minX;
            object.y = minY;
            object.z = minZ;
            object.length = std::max(maxX - minX, 0.1);
            object.width = std::max(maxY - minY, 0.1);
            object.height = std::max(maxZ - minZ, 0.1);
            object.curvePoints.reserve(worldPoints.size());
            for (const auto& point : worldPoints) {
                object.curvePoints.push_back(
                    {point.x - minX, point.y - minY, point.z - minZ});
            }
            try {
                object.customShape = rfsim::makePolylineCurve(object.curvePoints);
            } catch (const std::exception& exception) {
                QMessageBox::warning(
                    this, QStringLiteral("Curve creation failed"),
                    QString::fromUtf8(exception.what()));
                return;
            }
            recordGeometryFeature(object);
            objects_.push_back(std::move(object));
            const int row = static_cast<int>(objects_.size() - 1);
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Created a %1-point curve.")
                                 .arg(worldPoints.size()));
        });
        status_->setText(QStringLiteral(
            "Curve drawing: click to add points; change Curve plane Z between clicks for a 3D path. Click the first point to close, then right-click or double-click to finish."));
        statusBar()->showMessage(QStringLiteral("Draw mode — place curve points in Top View"));
    }

    void updatePortVisualization() {
        view_->setLumpedPort(portStartX_->value(), portStartY_->value(),
                             portStartZ_->value(), portStopX_->value(),
                             portStopY_->value(), portStopZ_->value());
    }

    QString createTemporaryResultsDirectory(QString* error) {
        auto directory = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + QStringLiteral("/rf-simulation-XXXXXX"));
        if (!directory->isValid()) {
            if (error) *error = QStringLiteral("Cannot create temporary results directory.");
            return {};
        }
        const QString path = directory->path();
        projectResultsTempDir_.reset();
        simulationResultsTempDir_ = std::move(directory);
        solverFiles_.clear();
        return path;
    }

    bool validPortCoordinates() const {
        const QString axis = portDirection_->currentData().toString();
        if (axis == QStringLiteral("x")) {
            return portStartX_->value() < portStopX_->value() &&
                   portStartY_->value() == portStopY_->value() &&
                   portStartZ_->value() == portStopZ_->value();
        }
        if (axis == QStringLiteral("y")) {
            return portStartY_->value() < portStopY_->value() &&
                   portStartX_->value() == portStopX_->value() &&
                   portStartZ_->value() == portStopZ_->value();
        }
        return axis == QStringLiteral("z") &&
               portStartZ_->value() < portStopZ_->value() &&
               portStartX_->value() == portStopX_->value() &&
               portStartY_->value() == portStopY_->value();
    }

    void updateParameterList() {
        const QSignalBlocker blocker(parameterTable_);
        parameterTable_->clearContents();
        if (geometryParameters_.empty()) {
            parameterTable_->setRowCount(1);
            auto* emptyItem =
                new QTableWidgetItem(QStringLiteral("No project parameters"));
            emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsEditable);
            parameterTable_->setItem(0, 0, emptyItem);
            return;
        }

        parameterTable_->setRowCount(
            static_cast<int>(geometryParameters_.size()));
        for (size_t index = 0; index < geometryParameters_.size(); ++index) {
            const auto& parameter = geometryParameters_[index];
            const int row = static_cast<int>(index);
            QString description = parameter.role.isEmpty()
                ? QStringLiteral("Project parameter")
                : parameter.role;
            if (parameter.optimizable) {
                description += QStringLiteral(" (%1 to %2, step %3)")
                                   .arg(parameter.minimum, 0, 'g', 8)
                                   .arg(parameter.maximum, 0, 'g', 8)
                                   .arg(parameter.step, 0, 'g', 8);
            }
            const std::array<QString, 5> values = {{
                parameter.name,
                parameter.expression,
                QString::number(parameter.evaluatedValue, 'g', 10),
                parameter.unit,
                description
            }};
            for (int column = 0; column < static_cast<int>(values.size());
                 ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                item->setData(Qt::UserRole, parameter.id);
                if (column == 4) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                } else if (column == 1) {
                    item->setToolTip(
                        QStringLiteral("Enter a numeric expression to update the value."));
                }
                parameterTable_->setItem(row, column, item);
            }
        }
    }

    void applyProjectParameterValues() {
        rfsim::OemProjectDocument project;
        project.solids = objects_;
        project.parameters = geometryParameters_;
        project.materials = geometryMaterials_;
        project.featureHistory = geometryHistory_;
        rfsim::applyOemParameterValues(&project);
        objects_ = std::move(project.solids);
        geometryParameters_ = std::move(project.parameters);
        geometryMaterials_ = std::move(project.materials);
        geometryHistory_ = std::move(project.featureHistory);
    }

    void updateProjectParameter(QTableWidgetItem* item) {
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        auto parameter = std::find_if(
            geometryParameters_.begin(), geometryParameters_.end(),
            [&id](const rfsim::OemParameter& value) {
                return value.id == id;
            });
        if (parameter == geometryParameters_.end()) return;

        const QString text = item->text().trimmed();
        if ((item->column() == 0 || item->column() == 3) && text.isEmpty()) {
            updateParameterList();
            statusBar()->showMessage(
                QStringLiteral("Parameter name and unit cannot be empty."));
            return;
        }
        if (item->column() == 0) {
            parameter->name = text;
        } else if (item->column() == 1 || item->column() == 2) {
            bool ok = false;
            const double value = text.toDouble(&ok);
            if (!ok || !std::isfinite(value) ||
                (parameter->optimizable &&
                 (value < parameter->minimum || value > parameter->maximum))) {
                updateParameterList();
                statusBar()->showMessage(
                    QStringLiteral("Enter a finite numeric value within the parameter bounds."));
                return;
            }
            if (item->column() == 1) {
                parameter->expression = text;
            } else {
                bool expressionIsNumeric = false;
                parameter->expression.toDouble(&expressionIsNumeric);
                if (expressionIsNumeric) {
                    parameter->expression =
                        QString::number(value, 'g', 17);
                }
            }
            parameter->evaluatedValue = value;
        } else if (item->column() == 3) {
            parameter->unit = text;
        } else {
            updateParameterList();
            return;
        }

        const int selectedRow = currentObjectIndex();
        QWidget* currentPage = workspace_->currentWidget();
        try {
            applyProjectParameterValues();
        } catch (const std::exception& exception) {
            updateParameterList();
            statusBar()->showMessage(
                QStringLiteral("Cannot apply parameter: %1")
                    .arg(QString::fromUtf8(exception.what())));
            return;
        }
        refreshList(selectedRow);
        loadSelected();
        loadSelectedMaterial();
        workspace_->setCurrentWidget(currentPage);
        updateParameterList();
        statusBar()->showMessage(QStringLiteral("Project parameter updated."));
    }

    bool loadProjectResultGraphs(const QString& directory, QString* error) {
        s11Plot_->clear();
        vswrPlot_->clear();
        wavePlot_->clear();
        for (const QString& name : projectResultFiles_.keys()) {
            if (!name.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
                continue;
            }
            const QString path = QDir(directory).filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                if (error) {
                    *error = QStringLiteral("Cannot read project result %1: %2")
                                 .arg(name, file.errorString());
                }
                return false;
            }
            const QStringList headers =
                QString::fromUtf8(file.readLine()).trimmed().split(',');
            if (headers.size() < 2) continue;

            const auto findColumn = [&headers](const QString& token,
                                               int startColumn,
                                               bool preferDb) {
                int firstMatch = -1;
                for (int column = startColumn; column < headers.size(); ++column) {
                    const QString label = headers[column].trimmed().toLower();
                    if (!label.contains(token)) continue;
                    if (firstMatch < 0) firstMatch = column;
                    if (preferDb && label.contains(QStringLiteral("db"))) {
                        return column;
                    }
                }
                return firstMatch;
            };
            const QString xHeader = headers.front().trimmed();
            const QString xKey = xHeader.toLower();
            const bool frequencyAxis = xKey.contains(QStringLiteral("frequency"));
            const bool timeAxis = xKey.contains(QStringLiteral("time"));
            const double xScale = frequencyAxis ? 1e-9 : timeAxis ? 1e9 : 1.0;
            const QString xLabel = frequencyAxis
                ? QStringLiteral("Frequency (GHz)")
                : timeAxis ? QStringLiteral("Time (ns)") : xHeader;

            const int s11Column =
                findColumn(QStringLiteral("s11"), 1, true);
            if (frequencyAxis && s11Column >= 0 && !s11Plot_->hasData()) {
                const int s21Column =
                    findColumn(QStringLiteral("s21"), 1, false);
                s11Plot_->loadCsv(
                    path, 0, s11Column, xScale, 1.0, s21Column,
                    headers[s11Column].trimmed(),
                    s21Column >= 0 ? headers[s21Column].trimmed() : QString());
            }

            const int vswrColumn =
                findColumn(QStringLiteral("vswr"), 1, false);
            if (frequencyAxis && vswrColumn >= 0 && !vswrPlot_->hasData()) {
                int secondVswrColumn = findColumn(
                    QStringLiteral("vswr"), vswrColumn + 1, false);
                vswrPlot_->loadCsv(
                    path, 0, vswrColumn, xScale, 1.0, secondVswrColumn,
                    headers[vswrColumn].trimmed(),
                    secondVswrColumn >= 0
                        ? headers[secondVswrColumn].trimmed()
                        : QString());
            }

            if (timeAxis && !wavePlot_->hasData()) {
                int voltageColumn =
                    findColumn(QStringLiteral("voltage"), 1, false);
                if (voltageColumn < 0) voltageColumn = 1;
                const int reflectedColumn =
                    findColumn(QStringLiteral("reflected"),
                               voltageColumn + 1, false);
                wavePlot_->loadCsv(
                    path, 0, voltageColumn, xScale, 1.0, reflectedColumn,
                    headers[voltageColumn].trimmed(),
                    reflectedColumn >= 0
                        ? headers[reflectedColumn].trimmed()
                        : QString());
            }
        }
        return true;
    }

    void newProject() {
        if ((hasActiveProject_ || !objects_.empty()) &&
            QMessageBox::question(
                this, QStringLiteral("New Project"),
                QStringLiteral("Discard the current model and start a new project?"))
                != QMessageBox::Yes) {
            return;
        }
        resetProjectState();
        hasActiveProject_ = true;
        setProjectCanvasAppearance(true);
        setWindowTitle(QStringLiteral("RF Simulation — Untitled Project"));
    }

    void setProjectCanvasAppearance(bool active) {
        const QString background = active ? QStringLiteral("#ffffff")
                                          : QStringLiteral("#e1e5e9");
        objectsList_->setStyleSheet(
            QStringLiteral("QTreeWidget { background: %1; border: 1px solid #c8d1dd; }")
                .arg(background));
        view_->setProjectActive(active);
    }

    void closeProject() {
        if ((!projectPath_.isEmpty() || !objects_.empty()) &&
            QMessageBox::question(
                this, QStringLiteral("Close Project"),
                QStringLiteral("Close the current project and discard unsaved changes?"))
                != QMessageBox::Yes) {
            return;
        }
        resetProjectState();
        setWindowTitle(QStringLiteral("RF Simulation"));
        statusBar()->showMessage(QStringLiteral("Project closed."));
    }

    void resetProjectState() {
        objects_.clear();
        projectPath_.clear();
        hasActiveProject_ = false;
        resultDirectory_.clear();
        projectResultsTempDir_.reset();
        simulationResultsTempDir_.reset();
        solverFiles_.clear();
        projectResultFiles_.clear();
        resultKind_ = QStringLiteral("none");
        projectSimulationSettings_ = {};
        geometryHistory_.clear();
        geometryParameters_.clear();
        geometryMaterials_.clear();
        s11Plot_->clear();
        vswrPlot_->clear();
        wavePlot_->clear();
        view_->setDrawMode({});
        topViewEnabled_ = false;
        workspace_->setCurrentWidget(workspace_->widget(0));
        refreshList(-1);
        loadSelected();
        loadSelectedMaterial();
        frequencyStart_->setValue(1.0);
        frequencyStop_->setValue(2.0);
        frequencyPoints_->setValue(201);
        meshDivisions_->setValue(30);
        maxTimesteps_->setValue(20000);
        boundary_->setCurrentIndex(0);
        portDirection_->setCurrentIndex(portDirection_->findData(QStringLiteral("z")));
        portStartX_->setValue(0.0);
        portStartY_->setValue(0.0);
        portStartZ_->setValue(0.0);
        portStopX_->setValue(0.0);
        portStopY_->setValue(0.0);
        portStopZ_->setValue(1.0);
        portImpedance_->setValue(50.0);
        updateParameterList();
        setProjectCanvasAppearance(false);
    }

    QStringList recentProjectPaths() const {
        QSettings settings(QStringLiteral("RFSim"), QStringLiteral("EMWorkbench"));
        QStringList paths =
            settings.value(QStringLiteral("recentProjects")).toStringList();
        paths.erase(std::remove_if(paths.begin(), paths.end(),
                                   [](const QString& path) {
                                       return !QFileInfo::exists(path);
                                   }),
                    paths.end());
        return paths;
    }

    void rememberRecentProject(const QString& path) {
        QStringList paths = recentProjectPaths();
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        paths.removeAll(absolutePath);
        paths.prepend(absolutePath);
        while (paths.size() > 10) paths.removeLast();
        QSettings settings(QStringLiteral("RFSim"), QStringLiteral("EMWorkbench"));
        settings.setValue(QStringLiteral("recentProjects"), paths);
    }

    void showRecentProjects(QWidget* anchor = nullptr) {
        QMenu menu(this);
        const QStringList paths = recentProjectPaths();
        if (paths.isEmpty()) {
            auto* emptyAction = menu.addAction(QStringLiteral("No recent projects"));
            emptyAction->setEnabled(false);
        } else {
            for (const QString& path : paths) {
                auto* action = menu.addAction(QFileInfo(path).fileName());
                action->setToolTip(path);
                action->setData(path);
            }
        }
        const QPoint menuPosition =
            anchor ? anchor->mapToGlobal(QPoint(0, anchor->height()))
                   : QCursor::pos();
        QAction* selected = menu.exec(menuPosition);
        if (selected && selected->isEnabled()) {
            openProjectAtPath(selected->data().toString());
        }
    }

    void openProject() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open RF simulation project"), projectPath_,
            QStringLiteral("OEM Project (*.oem)"));
        if (path.isEmpty()) return;
        openProjectAtPath(path);
    }

    void openProjectAtPath(const QString& path) {
        QString error;
        if (!loadProjectFile(path, &error)) {
            QMessageBox::critical(this, QStringLiteral("Open Project Failed"), error);
            return;
        }
        projectPath_ = path;
        hasActiveProject_ = true;
        setProjectCanvasAppearance(true);
        setWindowTitle(QStringLiteral("RF Simulation — %1")
                           .arg(QFileInfo(path).completeBaseName()));
        statusBar()->showMessage(QStringLiteral("Project loaded: %1").arg(path));
        rememberRecentProject(path);
    }

    void saveProject() {
        QString path = projectPath_;
        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save RF simulation project"),
                QStringLiteral("untitled.oem"),
                QStringLiteral("OEM Project (*.oem)"));
        }
        if (path.isEmpty()) return;
        saveProjectToPath(path);
    }

    void saveProjectAs() {
        const QString initialPath =
            projectPath_.isEmpty() ? QStringLiteral("untitled.oem") : projectPath_;
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save RF simulation project as"),
            initialPath, QStringLiteral("OEM Project (*.oem)"));
        if (path.isEmpty()) return;
        saveProjectToPath(path);
    }

    void saveProjectToPath(QString path) {
        if (!path.endsWith(QStringLiteral(".oem"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".oem");
        }
        QString error;
        if (!saveProjectFile(path, &error)) {
            QMessageBox::critical(this, QStringLiteral("Save Project Failed"), error);
            return;
        }
        projectPath_ = path;
        hasActiveProject_ = true;
        setProjectCanvasAppearance(true);
        setWindowTitle(QStringLiteral("RF Simulation — %1")
                           .arg(QFileInfo(path).completeBaseName()));
        statusBar()->showMessage(QStringLiteral("Project saved: %1").arg(path));
        rememberRecentProject(path);
    }

    bool saveProjectFile(const QString& path, QString* error) const {
        rfsim::OemProjectDocument project;
        project.solids = objects_;
        project.parameters = geometryParameters_;
        project.materials = geometryMaterials_;
        project.featureHistory = geometryHistory_;
        project.solverFiles = solverFiles_;
        project.simulation = projectSimulationSettings_;
        auto& simulation = project.simulation;
        simulation.frequencyStartGHz = frequencyStart_->value();
        simulation.frequencyStopGHz = frequencyStop_->value();
        simulation.frequencyPoints = static_cast<int>(frequencyPoints_->value());
        simulation.meshDivisions = static_cast<int>(meshDivisions_->value());
        simulation.maximumTimesteps = static_cast<int>(maxTimesteps_->value());
        simulation.boundary = boundary_->currentData().toString();
        simulation.portAxis = portDirection_->currentData().toString();
        simulation.portStartXmm = portStartX_->value();
        simulation.portStartYmm = portStartY_->value();
        simulation.portStartZmm = portStartZ_->value();
        simulation.portStopXmm = portStopX_->value();
        simulation.portStopYmm = portStopY_->value();
        simulation.portStopZmm = portStopZ_->value();
        simulation.portImpedanceOhm = portImpedance_->value();
        simulation.engine = engine_->currentData().toString();
        project.resultKind = resultKind_;
        project.resultFiles = projectResultFiles_;

        const auto readAsset = [&](const QString& filePath, QByteArray* data) {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) {
                if (error) {
                    *error = QStringLiteral("Cannot read project asset %1: %2")
                                 .arg(filePath, file.errorString());
                }
                return false;
            }
            *data = file.readAll();
            if (file.error() != QFileDevice::NoError) {
                if (error) {
                    *error = QStringLiteral("Cannot read project asset %1: %2")
                                 .arg(filePath, file.errorString());
                }
                return false;
            }
            return true;
        };

        if (resultKind_ == QStringLiteral("cad") &&
            !resultDirectory_.isEmpty() && QFileInfo(resultDirectory_).isDir()) {
            const QStringList requiredFiles = {
                QStringLiteral("s11_scene.csv"),
                QStringLiteral("s11_scene.s1p"),
                QStringLiteral("waveform_scene.csv")
            };
            const bool resultsComplete = std::all_of(
                requiredFiles.cbegin(), requiredFiles.cend(),
                [this](const QString& name) {
                    return QFileInfo::exists(
                        QDir(resultDirectory_).filePath(name));
                });
            if (resultsComplete) {
                project.resultFiles.clear();
                for (const QString& name : requiredFiles) {
                    QByteArray data;
                    if (!readAsset(QDir(resultDirectory_).filePath(name), &data)) {
                        return false;
                    }
                    project.resultFiles.insert(name, data);
                }
                const QDir resultRoot(resultDirectory_);
                const QStringList summaries = resultRoot.entryList(
                    {QStringLiteral("summary*.json")},
                    QDir::Files | QDir::NoDotAndDotDot);
                for (const QString& summary : summaries) {
                    QByteArray data;
                    if (!readAsset(resultRoot.filePath(summary), &data)) return false;
                    QJsonParseError parseError;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(data, &parseError);
                    if (parseError.error != QJsonParseError::NoError ||
                        !document.isObject()) {
                        if (error) {
                            *error = QStringLiteral("Invalid simulation summary %1: %2")
                                         .arg(summary, parseError.errorString());
                        }
                        return false;
                    }
                    QJsonObject summaryObject = document.object();
                    summaryObject.remove(QStringLiteral("simulation_directories"));
                    summaryObject.insert(QStringLiteral("raw_field_dumps_in_oem"), false);
                    project.resultFiles.insert(
                        summary,
                        QJsonDocument(summaryObject).toJson(QJsonDocument::Indented));
                }
            } else {
                project.resultKind = QStringLiteral("none");
                project.resultFiles.clear();
            }
        }

        if (!resultDirectory_.isEmpty() && QFileInfo(resultDirectory_).isDir()) {
            const QString modelsPath = QDir(resultDirectory_).filePath(
                QStringLiteral("models"));
            if (QFileInfo(modelsPath).isDir()) {
                QDirIterator models(modelsPath, QDir::Files | QDir::NoDotAndDotDot,
                                    QDirIterator::Subdirectories);
                const QDir modelRoot(QFileInfo(modelsPath).absoluteFilePath());
                while (models.hasNext()) {
                    const QString filePath = models.next();
                    const QFileInfo fileInfo(filePath);
                    if (fileInfo.isSymLink() || !fileInfo.isFile()) continue;
                    const QString suffix = fileInfo.suffix().toLower();
                    if (suffix != QStringLiteral("xml") &&
                        suffix != QStringLiteral("stl") &&
                        suffix != QStringLiteral("ply")) {
                        continue;
                    }
                    const QString relativePath = QDir::fromNativeSeparators(
                        modelRoot.relativeFilePath(
                            QFileInfo(filePath).absoluteFilePath()));
                    QByteArray data;
                    if (!readAsset(filePath, &data)) return false;
                    project.solverFiles.insert(relativePath, data);
                }
            }
        }
        return rfsim::writeOemProject(path, project, error);
    }

    bool loadProjectFile(const QString& path, QString* error) {
        rfsim::OemProjectDocument project;
        if (!rfsim::readOemProject(path, &project, error)) return false;

        std::unique_ptr<QTemporaryDir> stagedResults;
        QString stagedResultPath;
        if (project.resultKind != QStringLiteral("none")) {
            if (project.resultFiles.isEmpty()) {
                if (error) *error = QStringLiteral("Project result data is empty.");
                return false;
            }
            stagedResults = std::make_unique<QTemporaryDir>();
            if (!stagedResults->isValid()) {
                if (error) *error = QStringLiteral("Cannot create temporary result storage.");
                return false;
            }
            for (auto it = project.resultFiles.cbegin();
                 it != project.resultFiles.cend(); ++it) {
                const QString destination = stagedResults->filePath(it.key());
                if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
                    if (error) {
                        *error = QStringLiteral("Cannot stage project result file: %1")
                                     .arg(it.key());
                    }
                    return false;
                }
                QFile resultFile(destination);
                if (!resultFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    resultFile.write(it.value()) != it.value().size() ||
                    resultFile.error() != QFileDevice::NoError) {
                    if (error) {
                        *error = QStringLiteral("Cannot stage project result file %1: %2")
                                     .arg(it.key(), resultFile.errorString());
                    }
                    return false;
                }
            }
            stagedResultPath = stagedResults->path();
        }

        projectSimulationSettings_ = project.simulation;
        objects_ = std::move(project.solids);
        frequencyStart_->setValue(project.simulation.frequencyStartGHz);
        frequencyStop_->setValue(project.simulation.frequencyStopGHz);
        frequencyPoints_->setValue(project.simulation.frequencyPoints);
        meshDivisions_->setValue(project.simulation.meshDivisions);
        maxTimesteps_->setValue(project.simulation.maximumTimesteps);
        boundary_->setCurrentIndex(boundary_->findData(project.simulation.boundary));
        portDirection_->setCurrentIndex(
            portDirection_->findData(project.simulation.portAxis));
        portStartX_->setValue(project.simulation.portStartXmm);
        portStartY_->setValue(project.simulation.portStartYmm);
        portStartZ_->setValue(project.simulation.portStartZmm);
        portStopX_->setValue(project.simulation.portStopXmm);
        portStopY_->setValue(project.simulation.portStopYmm);
        portStopZ_->setValue(project.simulation.portStopZmm);
        portImpedance_->setValue(project.simulation.portImpedanceOhm);
        engine_->setCurrentIndex(engine_->findData(project.simulation.engine));

        resultDirectory_ = stagedResultPath;
        resultKind_ = project.resultKind;
        projectResultFiles_ = project.resultFiles;
        solverFiles_ = project.solverFiles;
        geometryHistory_ = project.featureHistory;
        geometryParameters_ = project.parameters;
        geometryMaterials_ = project.materials;
        s11Plot_->clear();
        vswrPlot_->clear();
        wavePlot_->clear();
        view_->setDrawMode({});
        view_->setTopView(false);
        topViewEnabled_ = false;
        refreshList(objects_.empty() ? -1 : 0);
        loadSelected();
        loadSelectedMaterial();
        updatePortVisualization();
        updateParameterList();
        if (!stagedResultPath.isEmpty() &&
            !loadProjectResultGraphs(stagedResultPath, error)) {
            return false;
        }
        if (s11Plot_->hasData()) {
            status_->setText(QStringLiteral("Project result graphs loaded."));
            resultPlots_->setCurrentWidget(s11Plot_);
        } else if (vswrPlot_->hasData()) {
            resultPlots_->setCurrentWidget(vswrPlot_);
        } else if (wavePlot_->hasData()) {
            resultPlots_->setCurrentWidget(wavePlot_);
        } else if (!projectResultFiles_.isEmpty()) {
            resultPlots_->setCurrentWidget(projectResultsList_);
        }
        workspace_->setCurrentWidget(workspace_->widget(0));
        projectResultsTempDir_ = std::move(stagedResults);
        simulationResultsTempDir_.reset();
        return true;
    }

    void duplicateSelected() {
        const int row = currentObjectIndex();
        if (row < 0 || row >= static_cast<int>(objects_.size())) return;
        auto copy = objects_[static_cast<size_t>(row)];
        copy.name += " copy";
        copy.inputFeatureIds = {copy.id};
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        copy.featureType = "transform";
        copy.materialId.clear();
        copy.x += 5.0;
        copy.y += 5.0;
        recordGeometryFeature(copy);
        objects_.push_back(std::move(copy));
        refreshList(static_cast<int>(objects_.size() - 1));
        loadSelected();
        loadSelectedMaterial();
    }

    std::vector<int> selectedObjectIndices() const {
        std::vector<int> indexes;
        for (const auto* item : objectsList_->selectedItems()) {
            const QVariant value = item->data(0, Qt::UserRole);
            if (value.isValid()) indexes.push_back(value.toInt());
        }
        std::sort(indexes.begin(), indexes.end());
        indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
        return indexes;
    }

    rfsim::SolidObject makeImportedObject(const TopoDS_Shape& shape,
                                          const QString& name) const {
        if (!rfsim::isValidShape(shape)) {
            throw std::runtime_error("The geometry is topologically invalid.");
        }
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        if (bounds.IsVoid()) {
            throw std::runtime_error("The geometry has no measurable bounds.");
        }
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        if (!(xMin < xMax && yMin < yMax && zMin < zMax)) {
            throw std::runtime_error(
                "Only three-dimensional solids can be imported or Boolean-combined.");
        }
        gp_Trsf normalize;
        normalize.SetTranslation(gp_Vec(-xMin, -yMin, -zMin));
        rfsim::SolidObject object;
        object.kind = rfsim::SolidKind::Imported;
        object.name = name.toStdString();
        object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        object.featureType = "step.import";
        object.x = xMin;
        object.y = yMin;
        object.z = zMin;
        object.length = xMax - xMin;
        object.width = yMax - yMin;
        object.height = zMax - zMin;
        object.customShape = BRepBuilderAPI_Transform(shape, normalize, true).Shape();
        return object;
    }

    rfsim::SolidObject makeSurfaceObject(const TopoDS_Shape& shape,
                                         const QString& name) const {
        if (!rfsim::isValidShape(shape)) {
            throw std::runtime_error("The swept surface is topologically invalid.");
        }
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        if (bounds.IsVoid()) {
            throw std::runtime_error("The surface has no measurable bounds.");
        }
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        const double zSpan = zMax - zMin;
        const std::array<double, 3> spans = {xSpan, ySpan, zSpan};
        if (std::count_if(spans.begin(), spans.end(),
                          [](double span) { return span > 1e-9; }) < 2) {
            throw std::runtime_error("A surface must span at least two axes.");
        }
        gp_Trsf normalize;
        normalize.SetTranslation(gp_Vec(-xMin, -yMin, -zMin));
        rfsim::SolidObject object;
        object.kind = rfsim::SolidKind::Surface;
        object.name = name.toStdString();
        object.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        object.featureType = "sweep.surface";
        object.x = xMin;
        object.y = yMin;
        object.z = zMin;
        object.length = std::max(xSpan, 0.1);
        object.width = std::max(ySpan, 0.1);
        object.height = std::max(zSpan, 0.1);
        object.customShape = BRepBuilderAPI_Transform(shape, normalize, true).Shape();
        return object;
    }

    void sweepSelectedCurvesToSurface() {
        const std::vector<int> indexes = selectedObjectIndices();
        if (indexes.size() != 2 ||
            objects_[static_cast<size_t>(indexes[0])].kind !=
                rfsim::SolidKind::Curve ||
            objects_[static_cast<size_t>(indexes[1])].kind !=
                rfsim::SolidKind::Curve) {
            QMessageBox::information(
                this, QStringLiteral("Select two curves"),
                QStringLiteral("Select a profile curve and a path curve, in that order."));
            return;
        }
        try {
            const auto& profile = objects_[static_cast<size_t>(indexes[0])];
            const auto& path = objects_[static_cast<size_t>(indexes[1])];
            auto surface = makeSurfaceObject(
                rfsim::sweepCurveToSurface(
                    TopoDS::Wire(rfsim::makeSolid(profile)),
                    TopoDS::Wire(rfsim::makeSolid(path))),
                QStringLiteral("Curve sweep surface"));
            surface.inputFeatureIds = {profile.id, path.id};
            surface.featureType = "sweep.surface";
            recordGeometryFeature(surface);
            objects_.push_back(std::move(surface));
            const int row = static_cast<int>(objects_.size() - 1);
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Created a swept surface."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("Curve sweep failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void sweepSelectedFaceToSolid() {
        const std::vector<int> indexes = selectedObjectIndices();
        if (indexes.size() != 2 ||
            objects_[static_cast<size_t>(indexes[0])].kind !=
                rfsim::SolidKind::Curve ||
            objects_[static_cast<size_t>(indexes[1])].kind !=
                rfsim::SolidKind::Curve) {
            QMessageBox::information(
                this, QStringLiteral("Select a closed profile and path"),
                QStringLiteral("Select a closed curve profile first and a path curve second."));
            return;
        }
        try {
            const auto& profile = objects_[static_cast<size_t>(indexes[0])];
            const auto& path = objects_[static_cast<size_t>(indexes[1])];
            const TopoDS_Wire profileWire =
                TopoDS::Wire(rfsim::makeSolid(profile));
            if (!BRep_Tool::IsClosed(profileWire)) {
                throw std::invalid_argument(
                    "The sweep profile curve must be closed to define a face.");
            }
            BRepBuilderAPI_MakeFace faceBuilder(profileWire);
            if (!faceBuilder.IsDone()) {
                throw std::runtime_error("The closed profile cannot form a face.");
            }
            auto solid = makeImportedObject(
                rfsim::sweepFaceToSolid(
                    faceBuilder.Face(),
                    TopoDS::Wire(rfsim::makeSolid(path))),
                QStringLiteral("Face sweep solid"));
            solid.featureType = "sweep.solid";
            solid.inputFeatureIds = {profile.id, path.id};
            recordGeometryFeature(solid);
            objects_.push_back(std::move(solid));
            const int row = static_cast<int>(objects_.size() - 1);
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Created a swept solid."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("Solid sweep failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void solidifySelectedSurface() {
        const std::vector<int> indexes = selectedObjectIndices();
        if (indexes.size() != 1 ||
            objects_[static_cast<size_t>(indexes.front())].kind !=
                rfsim::SolidKind::Surface) {
            QMessageBox::information(
                this, QStringLiteral("Select a closed surface"),
                QStringLiteral("Select one closed surface shell to convert to a solid."));
            return;
        }
        try {
            const auto& surface = objects_[static_cast<size_t>(indexes.front())];
            const TopoDS_Shape surfaceShape = rfsim::makeSolid(surface);
            TopoDS_Shell shell;
            if (surfaceShape.ShapeType() == TopAbs_SHELL) {
                shell = TopoDS::Shell(surfaceShape);
            } else {
                TopExp_Explorer shells(surfaceShape, TopAbs_SHELL);
                if (!shells.More()) {
                    throw std::invalid_argument(
                        "The selected surface does not contain a closed shell.");
                }
                shell = TopoDS::Shell(shells.Current());
            }
            auto solid = makeImportedObject(
                rfsim::solidifyClosedShell(shell),
                QStringLiteral("Solid from closed surface"));
            solid.featureType = "solidify.closed-shell";
            solid.inputFeatureIds = {surface.id};
            recordGeometryFeature(solid);
            objects_.push_back(std::move(solid));
            const int row = static_cast<int>(objects_.size() - 1);
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Converted the closed surface to a solid."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("Surface conversion failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void importStep() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import STEP geometry"), QString(),
            QStringLiteral("STEP files (*.step *.stp)"));
        if (path.isEmpty()) return;
        try {
            const TopoDS_Shape shape = rfsim::readStep(path.toStdString());
            const int row = static_cast<int>(objects_.size());
            auto object = makeImportedObject(
                shape, QFileInfo(path).completeBaseName());
            recordGeometryFeature(object);
            objects_.push_back(std::move(object));
            refreshList(row);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(QStringLiteral("Imported valid STEP geometry."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("STEP import failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void applyBooleanOperation(bool cut) {
        const std::vector<int> indexes = selectedObjectIndices();
        if (indexes.size() != 2) {
            QMessageBox::information(
                this, QStringLiteral("Select two solids"),
                QStringLiteral("Select exactly two geometry items in Navigation Tree. For Cut, the lower tree item is the tool/subtractor."));
            return;
        }
        if (objects_[static_cast<size_t>(indexes[0])].kind ==
                rfsim::SolidKind::Curve ||
            objects_[static_cast<size_t>(indexes[0])].kind ==
                rfsim::SolidKind::Surface ||
            objects_[static_cast<size_t>(indexes[1])].kind ==
                rfsim::SolidKind::Curve ||
            objects_[static_cast<size_t>(indexes[1])].kind ==
                rfsim::SolidKind::Surface) {
            QMessageBox::information(
                this, QStringLiteral("Select two solids"),
                QStringLiteral("Boolean operations require closed solid bodies."));
            return;
        }
        try {
            const auto first = rfsim::makeSolid(objects_[static_cast<size_t>(indexes[0])]);
            const auto second = rfsim::makeSolid(objects_[static_cast<size_t>(indexes[1])]);
            const TopoDS_Shape result = cut
                ? rfsim::cutShapes(first, second)
                : rfsim::fuseShapes(first, second);
            auto combined = makeImportedObject(
                result, cut ? QStringLiteral("Boolean Cut")
                            : QStringLiteral("Boolean Fuse"));
            combined.featureType = cut ? "boolean.cut" : "boolean.fuse";
            combined.inputFeatureIds = {
                objects_[static_cast<size_t>(indexes[0])].id,
                objects_[static_cast<size_t>(indexes[1])].id
            };
            const auto firstMaterial = objects_[static_cast<size_t>(indexes[0])].material;
            const auto secondMaterial = objects_[static_cast<size_t>(indexes[1])].material;
            combined.material = firstMaterial == secondMaterial
                                    ? firstMaterial
                                    : rfsim::MaterialKind::PerfectConductor;
            recordGeometryFeature(combined);
            for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
                objects_.erase(objects_.begin() + *it);
            }
            objects_.push_back(std::move(combined));
            const int selected = static_cast<int>(objects_.size() - 1);
            refreshList(selected);
            loadSelected();
            loadSelectedMaterial();
            status_->setText(cut ? QStringLiteral("Boolean subtraction completed.")
                                 : QStringLiteral("Boolean union completed."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("Boolean operation failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void booleanFuseSelected() { applyBooleanOperation(false); }
    void booleanCutSelected() { applyBooleanOperation(true); }

    void exportStep() {
        if (objects_.empty()) return;
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export CAD scene"),
            QStringLiteral("rf_scene.step"),
            QStringLiteral("STEP files (*.step *.stp)"));
        if (path.isEmpty()) return;
        try {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (const auto& object : objects_) {
                builder.Add(compound, rfsim::makeSolid(object));
            }
            rfsim::writeStep(compound, path.toStdString());
            status_->setText(QStringLiteral("STEP scene exported."));
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("STEP export failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void refreshProjectMaterialList() {
        const QSignalBlocker blocker(projectMaterialsList_);
        projectMaterialsList_->clearContents();
        projectMaterialsList_->setRowCount(
            static_cast<int>(geometryMaterials_.size()));
        for (size_t index = 0; index < geometryMaterials_.size(); ++index) {
            const auto& material = geometryMaterials_[index];
            const int row = static_cast<int>(index);
            const auto makeItem = [&material](const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setData(Qt::UserRole, material.id);
                return item;
            };
            projectMaterialsList_->setItem(
                row, 0, makeItem(material.name));
            projectMaterialsList_->setItem(
                row, 1, makeItem(material.type));
            projectMaterialsList_->setItem(
                row, 2, makeItem(QString::number(
                            material.relativePermittivity, 'g', 10)));
            projectMaterialsList_->setItem(
                row, 3, makeItem(QString::number(
                            material.conductivitySPerM, 'g', 10)));
            projectMaterialsList_->setItem(
                row, 4, makeItem(QString::number(
                            material.lossTangent, 'g', 10)));
        }
    }

    void updateProjectMaterial(QTableWidgetItem* item) {
        if (!item || item->row() < 0 ||
            item->row() >= projectMaterialsList_->rowCount()) {
            return;
        }
        const QString materialId =
            item->data(Qt::UserRole).toString();
        auto material = std::find_if(
            geometryMaterials_.begin(), geometryMaterials_.end(),
            [&materialId](const rfsim::OemMaterial& value) {
                return value.id == materialId;
            });
        if (material == geometryMaterials_.end()) return;

        const auto rejectEdit = [this](const QString& message) {
            refreshProjectMaterialList();
            statusBar()->showMessage(message);
        };
        const QString text = item->text().trimmed();
        if (item->column() == 0) {
            if (text.isEmpty()) {
                rejectEdit(QStringLiteral("Material name cannot be empty."));
                return;
            }
            material->name = text;
        } else if (item->column() == 1) {
            const QString type = text.toLower();
            if (type != QStringLiteral("pec") &&
                type != QStringLiteral("dielectric")) {
                rejectEdit(QStringLiteral("Material type must be PEC or dielectric."));
                return;
            }
            material->type = type;
        } else {
            bool ok = false;
            const double value = text.toDouble(&ok);
            const double minimum = item->column() == 2 ? 1.0 : 0.0;
            const double maximum = item->column() == 2
                ? 1000.0
                : item->column() == 3 ? 1.0e8 : 10.0;
            if (!ok || !std::isfinite(value) ||
                value < minimum || value > maximum) {
                rejectEdit(QStringLiteral("Material property value is outside its valid range."));
                return;
            }
            const QString argumentName = item->column() == 2
                ? QStringLiteral("relative-permittivity")
                : item->column() == 3
                    ? QStringLiteral("conductivity")
                    : QStringLiteral("loss-tangent");
            for (const auto& feature : geometryHistory_) {
                if (feature.materialRef != materialId) continue;
                for (const auto& argument : feature.arguments) {
                    const QString name = argument.name;
                    const bool matches = name == argumentName ||
                        (argumentName == QStringLiteral("relative-permittivity") &&
                         name == QStringLiteral("relative_permittivity")) ||
                        (argumentName == QStringLiteral("loss-tangent") &&
                         name == QStringLiteral("loss_tangent"));
                    if (!matches || argument.parameterRef.isEmpty()) continue;
                    const auto parameter = std::find_if(
                        geometryParameters_.begin(), geometryParameters_.end(),
                        [&argument](const rfsim::OemParameter& candidate) {
                            return candidate.id == argument.parameterRef;
                        });
                    if (parameter != geometryParameters_.end() &&
                        parameter->optimizable &&
                        (value < parameter->minimum ||
                         value > parameter->maximum)) {
                        rejectEdit(
                            QStringLiteral("Material value is outside a linked design-variable range."));
                        return;
                    }
                }
            }
            if (item->column() == 2) {
                material->relativePermittivity = value;
            } else if (item->column() == 3) {
                material->conductivitySPerM = value;
            } else {
                material->lossTangent = value;
            }
            for (auto& feature : geometryHistory_) {
                if (feature.materialRef != materialId) continue;
                for (auto& argument : feature.arguments) {
                    const QString name = argument.name;
                    const bool matches = name == argumentName ||
                        (argumentName == QStringLiteral("relative-permittivity") &&
                         name == QStringLiteral("relative_permittivity")) ||
                        (argumentName == QStringLiteral("loss-tangent") &&
                         name == QStringLiteral("loss_tangent"));
                    if (!matches) continue;
                    argument.expression = QString::number(value, 'g', 17);
                    argument.evaluatedValue = value;
                    auto parameter = std::find_if(
                        geometryParameters_.begin(), geometryParameters_.end(),
                        [&argument](const rfsim::OemParameter& candidate) {
                            return candidate.id == argument.parameterRef;
                        });
                    if (parameter != geometryParameters_.end()) {
                        parameter->expression = argument.expression;
                        parameter->evaluatedValue = value;
                    }
                }
            }
        }

        for (auto& object : objects_) {
            if (QString::fromStdString(object.materialId) != materialId) continue;
            object.material = material->type == QStringLiteral("dielectric")
                ? rfsim::MaterialKind::Dielectric
                : rfsim::MaterialKind::PerfectConductor;
            object.relativePermittivity = material->relativePermittivity;
            object.conductivity = material->conductivitySPerM;
            object.lossTangent = material->lossTangent;
        }

        const int selectedRow = currentObjectIndex();
        QWidget* currentPage = workspace_->currentWidget();
        try {
            applyProjectParameterValues();
        } catch (const std::exception& exception) {
            rejectEdit(QStringLiteral("Cannot apply material edit: %1")
                           .arg(QString::fromUtf8(exception.what())));
            return;
        }
        refreshList(selectedRow);
        loadSelected();
        loadSelectedMaterial();
        workspace_->setCurrentWidget(currentPage);
        updateParameterList();
        statusBar()->showMessage(QStringLiteral("Project material updated."));
    }

    void refreshList(int row) {
        objectsList_->clear();
        auto* geometry = new QTreeWidgetItem(objectsList_,
                                             {QStringLiteral("Geometry")});
        for (size_t i = 0; i < objects_.size(); ++i) {
            const auto& object = objects_[i];
            auto* item = new QTreeWidgetItem(
                geometry, {QString::fromStdString(object.name) +
                           QStringLiteral(" — ") + kindName(object.kind)});
            item->setData(0, Qt::UserRole, static_cast<int>(i));
        }
        if (objects_.empty()) {
            for (const auto& feature : geometryHistory_) {
                const QString label = feature.type.isEmpty()
                    ? feature.id
                    : feature.type;
                auto* featureItem = new QTreeWidgetItem(
                    geometry, {label + QStringLiteral(" — ") +
                               feature.outputKind});
                featureItem->setToolTip(0, feature.id);
                for (const auto& argument : feature.arguments) {
                    const QString value = argument.expression.isEmpty()
                        ? QString::number(argument.evaluatedValue, 'g', 8)
                        : argument.expression;
                    new QTreeWidgetItem(
                        featureItem,
                        {QStringLiteral("%1 = %2 %3")
                             .arg(argument.name, value, argument.unit)
                             .trimmed()});
                }
            }
        }
        auto* materials = new QTreeWidgetItem(objectsList_,
                                               {QStringLiteral("Materials")});
        if (!geometryMaterials_.empty()) {
            for (const auto& material : geometryMaterials_) {
                const QString label =
                    QStringLiteral("%1 — %2").arg(material.name, material.type);
                auto* item = new QTreeWidgetItem(materials, {label});
                item->setToolTip(0, material.id);
            }
        } else {
            for (size_t i = 0; i < objects_.size(); ++i) {
                const auto& object = objects_[i];
                const QString label =
                    QString::fromStdString(object.name) +
                    QStringLiteral(" — ") +
                    (object.material == rfsim::MaterialKind::PerfectConductor
                         ? QStringLiteral("PEC")
                         : QStringLiteral("Dielectric"));
                auto* item = new QTreeWidgetItem(materials, {label});
                item->setData(0, Qt::UserRole, static_cast<int>(i));
            }
        }
        refreshProjectMaterialList();
        auto* excitations = new QTreeWidgetItem(objectsList_,
                                                 {QStringLiteral("Excitations")});
        auto* port = new QTreeWidgetItem(
            excitations,
            {QStringLiteral("Lumped port — %1")
                 .arg(portDirection_->currentData().toString().toUpper())});
        new QTreeWidgetItem(
            port, {QStringLiteral("Start: (%1, %2, %3) mm")
                       .arg(portStartX_->value())
                       .arg(portStartY_->value())
                       .arg(portStartZ_->value())});
        new QTreeWidgetItem(
            port, {QStringLiteral("Stop: (%1, %2, %3) mm")
                       .arg(portStopX_->value())
                       .arg(portStopY_->value())
                       .arg(portStopZ_->value())});
        new QTreeWidgetItem(
            port, {QStringLiteral("Impedance: %1 Ω")
                       .arg(portImpedance_->value())});
        for (const QString& assetPath : solverFiles_.keys()) {
            const QString assetName = QFileInfo(assetPath).fileName();
            if (assetName.contains(QStringLiteral("port"), Qt::CaseInsensitive)) {
                new QTreeWidgetItem(
                    excitations,
                    {QStringLiteral("Solver asset: %1").arg(assetPath)});
            }
        }
        auto* results = new QTreeWidgetItem(objectsList_,
                                             {QStringLiteral("Results")});
        projectResultsList_->clear();
        QStringList resultNames = projectResultFiles_.keys();
        if (resultNames.isEmpty() && !resultDirectory_.isEmpty() &&
            QFileInfo(resultDirectory_).isDir()) {
            resultNames = QDir(resultDirectory_).entryList(
                QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        }
        for (const QString& name : resultNames) {
            const auto result = projectResultFiles_.constFind(name);
            const QString sizeLabel = result == projectResultFiles_.cend()
                ? QString()
                : QStringLiteral(" (%1 bytes)").arg(result->size());
            const QString label = name + sizeLabel;
            new QTreeWidgetItem(results, {label});
            new QTreeWidgetItem(projectResultsList_, {label});
        }
        objectsList_->expandAll();
        if (row >= 0 && row < static_cast<int>(objects_.size())) {
            objectsList_->setCurrentItem(geometry->child(row));
        }
        view_->setObjects(objects_);
        view_->setSelectedIndex(row);
    }

    int currentObjectIndex() const {
        const auto* item = objectsList_->currentItem();
        if (!item) return -1;
        const QVariant index = item->data(0, Qt::UserRole);
        return index.isValid() ? index.toInt() : -1;
    }

    void loadSelected() {
        const int row = currentObjectIndex();
        const bool valid = row >= 0 && row < static_cast<int>(objects_.size());
        for (auto* control : {x_, y_, z_, length_, width_, height_, radius_,
                              holeRadius_, rotationX_, rotationY_, rotationZ_}) {
            control->setEnabled(valid);
        }
        if (!valid) return;
        const auto& object = objects_[row];
        const bool parametricSolid =
            object.kind != rfsim::SolidKind::Imported &&
            object.kind != rfsim::SolidKind::Curve &&
            object.kind != rfsim::SolidKind::Surface;
        const QSignalBlocker b1(x_), b2(y_), b3(z_), b4(length_), b5(width_);
        const QSignalBlocker b6(height_), b7(radius_), b8(holeRadius_);
        const QSignalBlocker b9(rotationX_), b10(rotationY_), b11(rotationZ_);
        x_->setValue(object.x);
        y_->setValue(object.y);
        z_->setValue(object.z);
        length_->setValue(object.length);
        width_->setValue(object.width);
        height_->setValue(object.height);
        radius_->setValue(object.radius);
        holeRadius_->setValue(object.holeRadius);
        rotationX_->setValue(object.rotationXDegrees);
        rotationY_->setValue(object.rotationYDegrees);
        rotationZ_->setValue(object.rotationZDegrees);
        length_->setEnabled(parametricSolid &&
                            object.kind != rfsim::SolidKind::Cylinder);
        width_->setEnabled(parametricSolid &&
                           object.kind != rfsim::SolidKind::Cylinder);
        height_->setEnabled(parametricSolid);
        radius_->setEnabled(object.kind == rfsim::SolidKind::Cylinder);
        holeRadius_->setEnabled(object.kind == rfsim::SolidKind::PlateWithHole);
        rotationX_->setEnabled(true);
        rotationY_->setEnabled(true);
        rotationZ_->setEnabled(true);
        view_->setSelectedIndex(row);
    }

    void updateSelected() {
        const int row = currentObjectIndex();
        if (row < 0 || row >= static_cast<int>(objects_.size())) return;
        auto& object = objects_[row];
        object.x = x_->value();
        object.y = y_->value();
        object.z = z_->value();
        object.rotationXDegrees = rotationX_->value();
        object.rotationYDegrees = rotationY_->value();
        object.rotationZDegrees = rotationZ_->value();
        if (object.kind != rfsim::SolidKind::Imported &&
            object.kind != rfsim::SolidKind::Curve &&
            object.kind != rfsim::SolidKind::Surface) {
            object.length = length_->value();
            object.width = width_->value();
            object.height = height_->value();
            object.radius = radius_->value();
            object.holeRadius = std::min(holeRadius_->value(),
                                         std::min(object.length, object.width) * 0.49);
        }
        if (object.kind == rfsim::SolidKind::PlateWithHole) {
            const QSignalBlocker blocker(holeRadius_);
            holeRadius_->setValue(object.holeRadius);
        }
        recordGeometryFeature(object);
        view_->setObjects(objects_);
    }

    void loadSelectedMaterial() {
        const int row = currentObjectIndex();
        const bool valid = row >= 0 && row < static_cast<int>(objects_.size());
        selectedMaterialLabel_->setText(
            valid ? QString::fromStdString(objects_[row].name)
                  : QStringLiteral("No solid selected"));
        materialKind_->setEnabled(valid);
        relativePermittivity_->setEnabled(valid);
        conductivity_->setEnabled(valid);
        lossTangent_->setEnabled(valid);
        if (!valid) return;
        const auto& object = objects_[row];
        const QSignalBlocker b1(materialKind_), b2(relativePermittivity_);
        const QSignalBlocker b3(conductivity_), b4(lossTangent_);
        const int index = materialKind_->findData(static_cast<int>(object.material));
        materialKind_->setCurrentIndex(index);
        relativePermittivity_->setValue(object.relativePermittivity);
        conductivity_->setValue(object.conductivity);
        lossTangent_->setValue(object.lossTangent);
        const bool dielectric =
            object.material == rfsim::MaterialKind::Dielectric;
        relativePermittivity_->setEnabled(dielectric);
        conductivity_->setEnabled(dielectric);
        lossTangent_->setEnabled(dielectric);
    }

    void updateSelectedMaterial() {
        const int row = currentObjectIndex();
        if (row < 0 || row >= static_cast<int>(objects_.size())) return;
        auto& object = objects_[row];
        object.material = static_cast<rfsim::MaterialKind>(
            materialKind_->currentData().toInt());
        object.relativePermittivity = relativePermittivity_->value();
        object.conductivity = conductivity_->value();
        object.lossTangent = lossTangent_->value();
        recordGeometryFeature(object);
        const bool dielectric =
            object.material == rfsim::MaterialKind::Dielectric;
        relativePermittivity_->setEnabled(dielectric);
        conductivity_->setEnabled(dielectric);
        lossTangent_->setEnabled(dielectric);
    }

    void runSimulation(const QString& outputOverride = QString()) {
        if (objects_.empty()) {
            QMessageBox::warning(this, QStringLiteral("No CAD solids"),
                                 QStringLiteral("Add at least one solid before running the solver."));
            return;
        }
        if (frequencyStart_->value() >= frequencyStop_->value()) {
            QMessageBox::warning(
                this, QStringLiteral("Invalid frequency band"),
                QStringLiteral("Frequency stop must be greater than frequency start."));
            return;
        }
        if (!validPortCoordinates()) {
            QMessageBox::warning(
                this, QStringLiteral("Invalid port"),
                QStringLiteral("Port start/stop must differ only along the selected X/Y/Z axis, with start before stop."));
            return;
        }
        const QString output = outputOverride.isEmpty()
            ? QFileDialog::getExistingDirectory(
                  this, QStringLiteral("Choose solver output directory"),
                  QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            : outputOverride;
        if (output.isEmpty()) return;

        try {
            const QString sceneDirectory = output + QStringLiteral("/cad_scene");
            if (!QDir().mkpath(sceneDirectory)) {
                throw std::runtime_error("Cannot create the CAD scene output directory.");
            }
            QJsonArray solids;
            double minX = std::numeric_limits<double>::max();
            double minY = minX;
            double minZ = minX;
            double maxX = std::numeric_limits<double>::lowest();
            double maxY = maxX;
            double maxZ = maxX;
            std::vector<TopoDS_Shape> validatedShapes;
            std::vector<QString> stlPaths;
            validatedShapes.reserve(objects_.size());
            stlPaths.reserve(objects_.size());
            size_t solverSolidCount = 0;
            for (size_t i = 0; i < objects_.size(); ++i) {
                const auto& object = objects_[i];
                if (object.kind == rfsim::SolidKind::Curve) continue;
                if (object.kind == rfsim::SolidKind::Surface) {
                    const QString objectId = QString::fromStdString(object.id);
                    const bool convertedToSolid = std::any_of(
                        geometryHistory_.begin(), geometryHistory_.end(),
                        [&objectId](const rfsim::OemFeature& feature) {
                            return feature.type ==
                                       QStringLiteral("solidify.closed-shell") &&
                                   feature.inputs.contains(objectId);
                        });
                    if (convertedToSolid) continue;
                    throw std::runtime_error(
                        "Open or unconverted surfaces are not volume geometry for openEMS. Convert a closed shell to a solid first.");
                }
                const QString stl = sceneDirectory +
                                    QStringLiteral("/solid_%1.stl").arg(i);
                const TopoDS_Shape shape = rfsim::makeSolid(object);
                if (!rfsim::isValidShape(shape)) {
                    throw std::runtime_error(
                        "Invalid geometry for solid: " + object.name);
                }
                Bnd_Box shapeBounds;
                BRepBndLib::Add(shape, shapeBounds);
                if (shapeBounds.IsVoid()) {
                    throw std::runtime_error(
                        "Solid has no measurable bounds: " + object.name);
                }
                double solidMinX, solidMinY, solidMinZ;
                double solidMaxX, solidMaxY, solidMaxZ;
                shapeBounds.Get(solidMinX, solidMinY, solidMinZ,
                                solidMaxX, solidMaxY, solidMaxZ);
                minX = std::min(minX, solidMinX);
                minY = std::min(minY, solidMinY);
                minZ = std::min(minZ, solidMinZ);
                maxX = std::max(maxX, solidMaxX);
                maxY = std::max(maxY, solidMaxY);
                maxZ = std::max(maxZ, solidMaxZ);
                validatedShapes.push_back(shape);
                stlPaths.push_back(stl);
                ++solverSolidCount;
                QJsonObject solid;
                solid.insert(QStringLiteral("name"), QString::fromStdString(object.name));
                solid.insert(QStringLiteral("stl"), QFileInfo(stl).absoluteFilePath());
                solid.insert(QStringLiteral(
                                 "material_type"),
                             object.material == rfsim::MaterialKind::PerfectConductor
                                 ? QStringLiteral("pec")
                                 : QStringLiteral("dielectric"));
                solid.insert(QStringLiteral("relative_permittivity"),
                             object.relativePermittivity);
                solid.insert(QStringLiteral("conductivity_s_per_m"),
                             object.conductivity);
                solid.insert(QStringLiteral("loss_tangent"), object.lossTangent);
                solids.append(solid);
            }
            if (solverSolidCount == 0) {
                throw std::runtime_error(
                    "The scene contains no closed solid bodies to simulate.");
            }
            for (size_t i = 0; i < validatedShapes.size(); ++i) {
                rfsim::writeStl(validatedShapes[i], stlPaths[i].toStdString());
            }
            const QString scenePath = sceneDirectory + QStringLiteral("/scene.json");
            QJsonObject scene;
            scene.insert(QStringLiteral("solids"), solids);
            scene.insert(QStringLiteral("bounds_mm"),
                         QJsonArray{minX, minY, minZ, maxX, maxY, maxZ});
            const QJsonArray portStart = {
                portStartX_->value(), portStartY_->value(), portStartZ_->value()
            };
            const QJsonArray portStop = {
                portStopX_->value(), portStopY_->value(), portStopZ_->value()
            };
            scene.insert(QStringLiteral("port_start_mm"), portStart);
            scene.insert(QStringLiteral("port_stop_mm"), portStop);
            scene.insert(QStringLiteral("port_axis"),
                         portDirection_->currentData().toString());
            scene.insert(QStringLiteral("port_x_mm"), portStartX_->value());
            scene.insert(QStringLiteral("port_y_mm"), portStartY_->value());
            scene.insert(QStringLiteral("port_z_start_mm"), portStartZ_->value());
            scene.insert(QStringLiteral("port_z_stop_mm"), portStopZ_->value());
            QFile sceneFile(scenePath);
            if (!sceneFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                throw std::runtime_error("Cannot write the CAD solver scene file.");
            }
            sceneFile.write(QJsonDocument(scene).toJson(QJsonDocument::Indented));
            sceneFile.close();

            resultDirectory_ = output;
            solverFiles_.clear();
            projectResultFiles_.clear();
            resultKind_ = QStringLiteral("cad");
            updateParameterList();
            log_->clear();
            status_->setText(QStringLiteral("Running openEMS on %1 CAD solid(s)…")
                                 .arg(objects_.size()));
            statusBar()->showMessage(QStringLiteral("CAD simulation running…"));
            runButton_->setEnabled(false);
            cancelButton_->setEnabled(true);
            cancelRequested_ = false;
            progressBar_->setRange(0, 0);
            progressBar_->show();
            solverProcess_->setProgram(qEnvironmentVariable(
                "OPENEMS_PYTHON", QString::fromUtf8(RF_SIM_PYTHON_DEFAULT)));
            solverProcess_->setArguments({
                QString::fromUtf8(RF_SIM_SOLVER_SCRIPT),
                QStringLiteral("--scene"), scenePath,
                QStringLiteral("--engine"), engine_->currentData().toString(),
                QStringLiteral("--output"), output,
                QStringLiteral("--timesteps"),
                QString::number(static_cast<int>(maxTimesteps_->value())),
                QStringLiteral("--mesh-div"),
                QString::number(static_cast<int>(meshDivisions_->value())),
                QStringLiteral("--f-start-ghz"),
                QString::number(frequencyStart_->value(), 'g', 12),
                QStringLiteral("--f-stop-ghz"),
                QString::number(frequencyStop_->value(), 'g', 12),
                QStringLiteral("--frequency-points"),
                QString::number(static_cast<int>(frequencyPoints_->value())),
                QStringLiteral("--boundary"),
                boundary_->currentData().toString(),
                QStringLiteral("--port-impedance"),
                QString::number(portImpedance_->value(), 'g', 12)
            });
            solverProcess_->start();
        } catch (const std::exception& exception) {
            QMessageBox::critical(this, QStringLiteral("CAD export failed"),
                                  QString::fromUtf8(exception.what()));
        }
    }

    void cancelSimulation() {
        if (solverProcess_->state() == QProcess::NotRunning) return;
        cancelRequested_ = true;
        cancelButton_->setEnabled(false);
        status_->setText(QStringLiteral("Cancelling solver…"));
        statusBar()->showMessage(QStringLiteral("Stopping solver…"));
        solverProcess_->terminate();
        QTimer::singleShot(2000, solverProcess_, [this] {
            if (solverProcess_->state() != QProcess::NotRunning) {
                solverProcess_->kill();
            }
        });
    }

    std::vector<rfsim::SolidObject> objects_;
    QString projectPath_;
    bool hasActiveProject_ = false;
    QString resultDirectory_;
    std::unique_ptr<QTemporaryDir> projectResultsTempDir_;
    std::unique_ptr<QTemporaryDir> simulationResultsTempDir_;
    QMap<QString, QByteArray> solverFiles_;
    QMap<QString, QByteArray> projectResultFiles_;
    rfsim::OemSimulationSettings projectSimulationSettings_;
    std::vector<rfsim::OemParameter> geometryParameters_;
    std::vector<rfsim::OemMaterial> geometryMaterials_;
    std::vector<rfsim::OemFeature> geometryHistory_;
    QString resultKind_ = QStringLiteral("none");
    bool topViewEnabled_ = false;
    bool gridVisible_ = true;
    QStackedWidget* workspace_ = nullptr;
    QWidget* resultsPage_ = nullptr;
    QTableWidget* projectMaterialsList_ = nullptr;
    QTreeWidget* projectResultsList_ = nullptr;
    QTreeWidget* objectsList_ = nullptr;
    QWidget* navigationPanel_ = nullptr;
    QDockWidget* navigationDock_ = nullptr;
    QWidget* analysisPanel_ = nullptr;
    QSplitter* contentSplitter_ = nullptr;
    QAction* analysisPanelAction_ = nullptr;
    QSplitter* editorSplitter_ = nullptr;
    QTabWidget* resultPlots_ = nullptr;
    ModelView* view_ = nullptr;
    QDoubleSpinBox* x_ = nullptr;
    QDoubleSpinBox* y_ = nullptr;
    QDoubleSpinBox* z_ = nullptr;
    QDoubleSpinBox* curvePlaneZ_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QDoubleSpinBox* holeRadius_ = nullptr;
    QDoubleSpinBox* rotationX_ = nullptr;
    QDoubleSpinBox* rotationY_ = nullptr;
    QDoubleSpinBox* rotationZ_ = nullptr;
    QDoubleSpinBox* portStartX_ = nullptr;
    QDoubleSpinBox* portStartY_ = nullptr;
    QDoubleSpinBox* portStartZ_ = nullptr;
    QDoubleSpinBox* portStopX_ = nullptr;
    QDoubleSpinBox* portStopY_ = nullptr;
    QDoubleSpinBox* portStopZ_ = nullptr;
    QComboBox* portDirection_ = nullptr;
    QDoubleSpinBox* portImpedance_ = nullptr;
    QDoubleSpinBox* frequencyStart_ = nullptr;
    QDoubleSpinBox* frequencyStop_ = nullptr;
    QDoubleSpinBox* frequencyPoints_ = nullptr;
    QDoubleSpinBox* meshDivisions_ = nullptr;
    QDoubleSpinBox* maxTimesteps_ = nullptr;
    QComboBox* boundary_ = nullptr;
    QLabel* selectedMaterialLabel_ = nullptr;
    QComboBox* materialKind_ = nullptr;
    QDoubleSpinBox* relativePermittivity_ = nullptr;
    QDoubleSpinBox* conductivity_ = nullptr;
    QDoubleSpinBox* lossTangent_ = nullptr;
    QComboBox* engine_ = nullptr;
    QToolButton* solverSelectionButton_ = nullptr;
    QToolButton* domainSelectionButton_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    PlotWidget* s11Plot_ = nullptr;
    PlotWidget* vswrPlot_ = nullptr;
    PlotWidget* wavePlot_ = nullptr;
    QTableWidget* parameterTable_ = nullptr;
    QProcess* solverProcess_ = nullptr;
    bool cancelRequested_ = false;
    bool smokeTestExpectedCancel_ = false;
    bool smokeTestMode_ = false;
    int smokeTestExitCode_ = 0;
};

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    MainWindow window;
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode.startsWith(QStringLiteral("--smoke-"))) {
        window.show();
    } else {
        window.showMaximized();
    }
    if (mode == QStringLiteral("--smoke-test")) {
        QTimer::singleShot(250, &application, &QApplication::quit);
    } else if (mode == QStringLiteral("--smoke-solver")) {
        window.startSimulationForSmokeTest();
    } else if (mode == QStringLiteral("--smoke-x-port")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startXAxisPortSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-draw")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startDrawSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-curve-draw")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startCurveDrawSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-direct-pick")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startDirectPickSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-project-io")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startProjectIoSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-empty-startup")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startEmptyStartupSmokeTest();
        });
    } else if (mode == QStringLiteral("--smoke-open-project")) {
        const QString path =
            argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
        QTimer::singleShot(0, &window, [&window, path] {
            window.startOpenProjectSmokeTest(path);
        });
    } else if (mode == QStringLiteral("--smoke-open-project-run")) {
        const QString path =
            argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
        const QString output =
            argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
        QTimer::singleShot(0, &window, [&window, path, output] {
            window.startOpenProjectSimulationSmokeTest(path, output);
        });
    } else if (mode == QStringLiteral("--smoke-cancel")) {
        QTimer::singleShot(0, &window, [&window] {
            window.startCancelSmokeTest();
        });
    }
    const int applicationResult = application.exec();
    if (mode == QStringLiteral("--smoke-solver") ||
        mode == QStringLiteral("--smoke-x-port") ||
        mode == QStringLiteral("--smoke-draw") ||
        mode == QStringLiteral("--smoke-curve-draw") ||
        mode == QStringLiteral("--smoke-direct-pick") ||
        mode == QStringLiteral("--smoke-project-io") ||
        mode == QStringLiteral("--smoke-empty-startup") ||
        mode == QStringLiteral("--smoke-open-project") ||
        mode == QStringLiteral("--smoke-open-project-run") ||
        mode == QStringLiteral("--smoke-cancel")) {
        return window.smokeTestExitCode();
    }
    return applicationResult;
}
