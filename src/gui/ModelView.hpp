#pragma once

#include "rfsim/Geometry.hpp"

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <vector>

class QPaintEvent;
class QMouseEvent;

class ModelView final : public QWidget {
public:
    explicit ModelView(QWidget* parent = nullptr);

    void setObjects(const std::vector<rfsim::SolidObject>& objects);
    void setSelectedIndex(int index);
    void setProjectActive(bool active);
    void setLumpedPort(double startX, double startY, double startZ,
                       double stopX, double stopY, double stopZ);
    void setDrawMode(
        std::function<void(double, double, double, double)> rectangleHandler);
    void setCurveDrawMode(
        std::function<void(const std::vector<rfsim::Point3D>&)> curveHandler);
    void setCurvePlaneZ(double zMm);
    void setTopView(bool enabled);
    void setSelectionHandler(std::function<void(int)> handler);
    void setShowGrid(bool enabled);
    void resetCamera();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPointF project(double x, double y, double z, double scale) const;
    double sceneScale() const;
    QPointF screenToWorld(const QPointF& point) const;
    void finishCurveDrawing();

    std::vector<rfsim::SolidObject> objects_;
    QPoint lastMousePosition_;
    int selectedIndex_ = -1;
    bool projectActive_ = false;
    bool topView_ = false;
    bool drawing_ = false;
    bool curveDrawing_ = false;
    double portStartX_ = 0.0;
    double portStartY_ = 0.0;
    double portStartZ_ = 0.0;
    double portStopX_ = 0.0;
    double portStopY_ = 0.0;
    double portStopZ_ = 1.0;
    double curveDrawZmm_ = 0.0;
    QPointF drawStart_;
    QPointF drawCurrent_;
    std::vector<rfsim::Point3D> curvePoints_;
    std::vector<QPointF> curveScreenPoints_;
    std::function<void(const std::vector<rfsim::Point3D>&)> curveHandler_;
    std::function<void(double, double, double, double)> rectangleHandler_;
    std::function<void(int)> selectionHandler_;
    std::vector<QRectF> projectedBounds_;
    bool showGrid_ = true;
    bool rotating_ = false;
    double azimuth_ = 0.75;
    double elevation_ = 0.5;
};
