#include "ModelView.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double pi = 3.14159265358979323846;
}

ModelView::ModelView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 220);
    setMouseTracking(true);
}

void ModelView::setObjects(const std::vector<rfsim::SolidObject>& objects) {
    objects_ = objects;
    update();
}

void ModelView::setSelectedIndex(int index) {
    selectedIndex_ = index;
    update();
}

void ModelView::setProjectActive(bool active) {
    projectActive_ = active;
    update();
}

void ModelView::setLumpedPort(double startX, double startY, double startZ,
                              double stopX, double stopY, double stopZ) {
    portStartX_ = startX;
    portStartY_ = startY;
    portStartZ_ = startZ;
    portStopX_ = stopX;
    portStopY_ = stopY;
    portStopZ_ = stopZ;
    update();
}

void ModelView::setDrawMode(
    std::function<void(double, double, double, double)> rectangleHandler) {
    rectangleHandler_ = std::move(rectangleHandler);
    curveHandler_ = {};
    curvePoints_.clear();
    curveScreenPoints_.clear();
    curveDrawing_ = false;
    drawing_ = false;
    topView_ = static_cast<bool>(rectangleHandler_);
    setCursor(topView_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ModelView::setCurveDrawMode(
    std::function<void(const std::vector<rfsim::Point3D>&)> curveHandler) {
    rectangleHandler_ = {};
    curveHandler_ = std::move(curveHandler);
    curvePoints_.clear();
    curveScreenPoints_.clear();
    curveDrawing_ = static_cast<bool>(curveHandler_);
    drawing_ = false;
    topView_ = curveDrawing_;
    setCursor(curveDrawing_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ModelView::setCurvePlaneZ(double zMm) {
    curveDrawZmm_ = zMm;
    update();
}

void ModelView::setTopView(bool enabled) {
    topView_ = enabled;
    update();
}

void ModelView::setSelectionHandler(std::function<void(int)> handler) {
    selectionHandler_ = std::move(handler);
}

void ModelView::setShowGrid(bool enabled) {
    showGrid_ = enabled;
    update();
}

void ModelView::resetCamera() {
    azimuth_ = 0.75;
    elevation_ = 0.5;
    topView_ = false;
    update();
}

QPointF ModelView::project(double x, double y, double z, double scale) const {
    if (topView_) {
        return {width() * 0.5 + x * scale,
                height() * 0.53 - y * scale};
    }
    const double rotatedX = x * std::cos(azimuth_) - y * std::sin(azimuth_);
    const double rotatedY = x * std::sin(azimuth_) + y * std::cos(azimuth_);
    const double projectedX = rotatedX - rotatedY;
    const double projectedY =
        (rotatedX + rotatedY) * std::sin(elevation_) - z * std::cos(elevation_);
    return {width() * 0.5 + projectedX * scale,
            height() * 0.53 - projectedY * scale};
}

double ModelView::sceneScale() const {
    double minX = 0.0, minY = 0.0, minZ = 0.0;
    double maxX = 40.0, maxY = 20.0, maxZ = 1.6;
    if (!objects_.empty()) {
        minX = minY = minZ = std::numeric_limits<double>::max();
        maxX = maxY = maxZ = std::numeric_limits<double>::lowest();
        for (const auto& object : objects_) {
            const double dx = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0 : object.length;
            const double dy = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0 : object.width;
            minX = std::min(minX, object.x);
            minY = std::min(minY, object.y);
            minZ = std::min(minZ, object.z);
            maxX = std::max(maxX, object.x + dx);
            maxY = std::max(maxY, object.y + dy);
            maxZ = std::max(maxZ, object.z + object.height);
        }
    }
    const double span = std::max({maxX - minX, maxY - minY, maxZ - minZ, 1.0});
    return std::min(width() * 0.72, height() * 0.72) / (span * 1.8);
}

QPointF ModelView::screenToWorld(const QPointF& point) const {
    double minX = 0.0, minY = 0.0, minZ = 0.0;
    double maxX = 40.0, maxY = 20.0, maxZ = 1.6;
    if (!objects_.empty()) {
        minX = minY = minZ = std::numeric_limits<double>::max();
        maxX = maxY = maxZ = std::numeric_limits<double>::lowest();
        for (const auto& object : objects_) {
            const double dx = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0 : object.length;
            const double dy = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0 : object.width;
            minX = std::min(minX, object.x);
            minY = std::min(minY, object.y);
            minZ = std::min(minZ, object.z);
            maxX = std::max(maxX, object.x + dx);
            maxY = std::max(maxY, object.y + dy);
            maxZ = std::max(maxZ, object.z + object.height);
        }
    }
    return {(point.x() - width() * 0.5) / sceneScale() + (minX + maxX) * 0.5,
            (height() * 0.53 - point.y()) / sceneScale() + (minY + maxY) * 0.5};
}

void ModelView::paintEvent(QPaintEvent*) {
    projectedBounds_.clear();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), projectActive_ ? Qt::white : QColor(225, 229, 233));

    double minX = 0.0, minY = 0.0, minZ = 0.0;
    double maxX = 40.0, maxY = 20.0, maxZ = 1.6;
    if (!objects_.empty()) {
        minX = minY = minZ = std::numeric_limits<double>::max();
        maxX = maxY = maxZ = std::numeric_limits<double>::lowest();
        for (const auto& object : objects_) {
            const double dx = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0
                                  : object.length;
            const double dy = object.kind == rfsim::SolidKind::Cylinder
                                  ? object.radius * 2.0
                                  : object.width;
            minX = std::min(minX, object.x);
            minY = std::min(minY, object.y);
            minZ = std::min(minZ, object.z);
            maxX = std::max(maxX, object.x + dx);
            maxY = std::max(maxY, object.y + dy);
            maxZ = std::max(maxZ, object.z + object.height);
        }
    }

    const double scale = sceneScale();
    const auto point = [this, scale, minX, maxX, minY, maxY](double x, double y, double z) {
        return project(x - (minX + maxX) * 0.5,
                       y - (minY + maxY) * 0.5, z, scale);
    };

    if (objects_.empty()) {
        painter.setPen(QColor(105, 115, 128));
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter,
                         QStringLiteral("No explicit solid geometry is stored in this project."));
    }

    if (showGrid_ && topView_) {
        painter.setPen(QPen(QColor(220, 226, 233), 1));
        const double gridStep = 10.0;
        const double firstX = std::ceil(minX / gridStep) * gridStep;
        const double firstY = std::ceil(minY / gridStep) * gridStep;
        for (double x = firstX; x <= maxX; x += gridStep) {
            painter.drawLine(point(x, minY, minZ), point(x, maxY, minZ));
        }
        for (double y = firstY; y <= maxY; y += gridStep) {
            painter.drawLine(point(minX, y, minZ), point(maxX, y, minZ));
        }
    }

    for (size_t index = 0; index < objects_.size(); ++index) {
        const auto& object = objects_[index];
        const bool cylinder = object.kind == rfsim::SolidKind::Cylinder;
        const double dx = cylinder ? object.radius * 2.0 : object.length;
        const double dy = cylinder ? object.radius * 2.0 : object.width;
        const double cx = cylinder ? object.x + object.radius : object.x;
        const double cy = cylinder ? object.y + object.radius : object.y;
        const double sx = cylinder ? object.radius : dx;
        const double sy = cylinder ? object.radius : dy;
        const auto objectPoint = [&](double localX, double localY, double localZ) {
            const double rx = object.rotationXDegrees * pi / 180.0;
            const double ry = object.rotationYDegrees * pi / 180.0;
            const double rz = object.rotationZDegrees * pi / 180.0;
            const double y1 = localY * std::cos(rx) - localZ * std::sin(rx);
            const double z1 = localY * std::sin(rx) + localZ * std::cos(rx);
            const double x2 = localX * std::cos(ry) + z1 * std::sin(ry);
            const double z2 = -localX * std::sin(ry) + z1 * std::cos(ry);
            const double x3 = x2 * std::cos(rz) - y1 * std::sin(rz);
            const double y3 = x2 * std::sin(rz) + y1 * std::cos(rz);
            return point(object.x + x3, object.y + y3, object.z + z2);
        };
        const std::array<QPointF, 8> corners = {{
            objectPoint(0, 0, 0), objectPoint(sx, 0, 0),
            objectPoint(sx, sy, 0), objectPoint(0, sy, 0),
            objectPoint(0, 0, object.height),
            objectPoint(sx, 0, object.height),
            objectPoint(sx, sy, object.height),
            objectPoint(0, sy, object.height)
        }};
        double screenMinX = corners[0].x(), screenMaxX = screenMinX;
        double screenMinY = corners[0].y(), screenMaxY = screenMinY;
        for (const auto& corner : corners) {
            screenMinX = std::min(screenMinX, corner.x());
            screenMaxX = std::max(screenMaxX, corner.x());
            screenMinY = std::min(screenMinY, corner.y());
            screenMaxY = std::max(screenMaxY, corner.y());
        }
        if (object.kind == rfsim::SolidKind::Curve) {
            QPolygonF curve;
            for (const auto& vertex : object.curvePoints) {
                curve.append(objectPoint(vertex.x, vertex.y, vertex.z));
            }
            if (curve.size() >= 2) {
                const QRectF bounds = curve.boundingRect().adjusted(-6, -6, 6, 6);
                projectedBounds_.push_back(bounds);
                painter.setPen(QPen(index == static_cast<size_t>(selectedIndex_)
                                        ? QColor(220, 91, 47)
                                        : QColor(36, 109, 168),
                                    index == static_cast<size_t>(selectedIndex_) ? 3 : 2));
                painter.drawPolyline(curve);
                painter.setBrush(QColor(36, 109, 168));
                for (const auto& vertex : curve) painter.drawEllipse(vertex, 3, 3);
                painter.setPen(QColor(37, 49, 66));
                painter.drawText(curve.front() + QPointF(5, -5),
                                 QString::fromStdString(object.name));
            }
            continue;
        }
        if (object.kind == rfsim::SolidKind::Surface) {
            double minScreenX = std::numeric_limits<double>::max();
            double minScreenY = minScreenX;
            double maxScreenX = std::numeric_limits<double>::lowest();
            double maxScreenY = maxScreenX;
            painter.setPen(QPen(index == static_cast<size_t>(selectedIndex_)
                                    ? QColor(220, 91, 47)
                                    : QColor(36, 109, 168),
                                index == static_cast<size_t>(selectedIndex_) ? 3 : 1.5));
            for (TopExp_Explorer edges(object.customShape, TopAbs_EDGE);
                 edges.More(); edges.Next()) {
                BRepAdaptor_Curve edge(TopoDS::Edge(edges.Current()));
                const double first = edge.FirstParameter();
                const double last = edge.LastParameter();
                if (!std::isfinite(first) || !std::isfinite(last) || last <= first) {
                    continue;
                }
                QPolygonF polyline;
                constexpr int segments = 24;
                for (int step = 0; step <= segments; ++step) {
                    const double parameter =
                        first + (last - first) * step / segments;
                    const gp_Pnt point3d = edge.Value(parameter);
                    const QPointF screenPoint =
                        objectPoint(point3d.X(), point3d.Y(), point3d.Z());
                    polyline.append(screenPoint);
                    minScreenX = std::min(minScreenX, screenPoint.x());
                    minScreenY = std::min(minScreenY, screenPoint.y());
                    maxScreenX = std::max(maxScreenX, screenPoint.x());
                    maxScreenY = std::max(maxScreenY, screenPoint.y());
                }
                if (polyline.size() > 1) painter.drawPolyline(polyline);
            }
            if (minScreenX <= maxScreenX && minScreenY <= maxScreenY) {
                projectedBounds_.emplace_back(
                    QPointF(minScreenX - 6, minScreenY - 6),
                    QPointF(maxScreenX + 6, maxScreenY + 6));
            } else {
                projectedBounds_.emplace_back(QPointF(0, 0), QPointF(0, 0));
            }
            painter.setPen(QColor(37, 49, 66));
            painter.drawText(objectPoint(0.0, 0.0, object.height) + QPointF(5, -5),
                             QString::fromStdString(object.name));
            continue;
        }
        projectedBounds_.emplace_back(QPointF(screenMinX, screenMinY),
                                      QPointF(screenMaxX, screenMaxY));

        painter.setPen(QPen(index == static_cast<size_t>(selectedIndex_)
                                ? QColor(220, 91, 47)
                                : QColor(48, 65, 87),
                            index == static_cast<size_t>(selectedIndex_) ? 3 : 1.5));
        painter.setBrush(QColor(107, 151, 196, 220));
        if (cylinder) {
            const QPointF top = objectPoint(sx, sy, object.height);
            const QPointF bottom = objectPoint(sx, sy, 0.0);
            const double rx = sx * scale * 1.35;
            const double ry = sy * scale * 0.75;
            painter.setBrush(QColor(174, 204, 230, 220));
            painter.drawEllipse(top, rx, ry);
            painter.setBrush(QColor(79, 120, 163, 220));
            painter.drawEllipse(bottom, rx, ry);
            painter.drawLine(top + QPointF(0, -ry), bottom + QPointF(0, -ry));
            painter.drawLine(top + QPointF(0, ry), bottom + QPointF(0, ry));
        } else {
            painter.drawPolygon(QPolygonF(
                {corners[0], corners[1], corners[5], corners[4]}));
            painter.setBrush(QColor(79, 120, 163, 220));
            painter.drawPolygon(QPolygonF(
                {corners[1], corners[2], corners[6], corners[5]}));
            painter.setBrush(QColor(174, 204, 230, 220));
            painter.drawPolygon(QPolygonF(
                {corners[4], corners[5], corners[6], corners[7]}));
        }
        if (object.kind == rfsim::SolidKind::PlateWithHole) {
            const QPointF center = objectPoint(object.length * 0.5,
                                               object.width * 0.5,
                                               object.height);
            painter.setBrush(QColor(247, 249, 252));
            painter.drawEllipse(center, object.holeRadius * scale * 1.35,
                               object.holeRadius * scale * 0.78);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, object.holeRadius * scale * 1.35,
                               object.holeRadius * scale * 0.78);
        }
        painter.setPen(QColor(37, 49, 66));
        painter.drawText(objectPoint(sx * 0.5, sy * 0.5, object.height) +
                             QPointF(5, -5),
                         QString::fromStdString(object.name));
    }

    if (!objects_.empty()) {
        const QPointF start = point(portStartX_, portStartY_, portStartZ_);
        const QPointF stop = point(portStopX_, portStopY_, portStopZ_);
        painter.setPen(QPen(QColor(192, 67, 45), 3));
        painter.drawLine(start, stop);
        painter.drawLine(start + QPointF(-6, -6), start + QPointF(6, 6));
        painter.drawLine(start + QPointF(-6, 6), start + QPointF(6, -6));
        painter.setPen(QColor(128, 45, 37));
        painter.drawText(stop + QPointF(8, -8), QStringLiteral("Lumped Port"));
    }
    if (curveDrawing_) {
        QPolygonF curve;
        for (const auto& point : curveScreenPoints_) curve.append(point);
        if (!curvePoints_.empty()) curve.append(drawCurrent_);
        painter.setPen(QPen(QColor(220, 91, 47), 2));
        if (curve.size() > 1) painter.drawPolyline(curve);
        painter.setBrush(QColor(220, 91, 47));
        for (const auto& point : curveScreenPoints_) painter.drawEllipse(point, 3, 3);
        painter.setPen(QColor(70, 77, 89));
        painter.drawText(QRect(16, height() - 28, width() - 32, 20),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Curve: click to add points, click the first point to close, right-click or double-click to finish"));
    } else if (drawing_) {
        const QRectF preview(drawStart_, drawCurrent_);
        painter.setPen(QPen(QColor(220, 91, 47), 2, Qt::DashLine));
        painter.setBrush(QColor(220, 91, 47, 45));
        painter.drawRect(preview.normalized());
        const QPointF start = screenToWorld(drawStart_);
        const QPointF current = screenToWorld(drawCurrent_);
        painter.setPen(QColor(90, 60, 35));
        painter.drawText(preview.normalized().bottomRight() + QPointF(8, -4),
                         QStringLiteral("%1 × %2 mm")
                             .arg(std::abs(current.x() - start.x()), 0, 'f', 1)
                             .arg(std::abs(current.y() - start.y()), 0, 'f', 1));
    }
    painter.setPen(QColor(37, 49, 66));
    painter.drawText(QRect(16, 12, width() - 32, 24),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     topView_ ? QStringLiteral("Top view — drag to draw; use 3D View to rotate")
                              : QStringLiteral("3D model — drag to rotate"));
}

void ModelView::mousePressEvent(QMouseEvent* event) {
    if (curveDrawing_) {
        if (event->button() == Qt::RightButton) {
            finishCurveDrawing();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            const QPointF screenPoint = event->position();
            const QPointF worldPoint = screenToWorld(screenPoint);
            if (curvePoints_.size() >= 3 &&
                std::hypot(curvePoints_.front().x - worldPoint.x(),
                           curvePoints_.front().y - worldPoint.y()) <= 0.5 &&
                std::abs(curvePoints_.front().z - curveDrawZmm_) <= 0.5) {
                curvePoints_.push_back(curvePoints_.front());
                curveScreenPoints_.push_back(curveScreenPoints_.front());
                finishCurveDrawing();
                event->accept();
                return;
            }
            if (curvePoints_.empty() ||
                std::hypot(curvePoints_.back().x - worldPoint.x(),
                           curvePoints_.back().y - worldPoint.y()) > 0.1 ||
                std::abs(curvePoints_.back().z - curveDrawZmm_) > 0.1) {
                curvePoints_.push_back({worldPoint.x(), worldPoint.y(), curveDrawZmm_});
                curveScreenPoints_.push_back(screenPoint);
            }
            drawCurrent_ = screenPoint;
            update();
            event->accept();
            return;
        }
    }
    if (rectangleHandler_ && event->button() == Qt::LeftButton) {
        drawStart_ = event->position();
        drawCurrent_ = drawStart_;
        drawing_ = true;
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && selectionHandler_) {
        const QPointF position = event->position();
        for (int index = static_cast<int>(projectedBounds_.size()) - 1;
             index >= 0; --index) {
            if (projectedBounds_[static_cast<size_t>(index)].contains(position)) {
                selectedIndex_ = index;
                selectionHandler_(index);
                update();
                return;
            }
        }
        selectedIndex_ = -1;
        selectionHandler_(-1);
        rotating_ = true;
    }
    lastMousePosition_ = event->position().toPoint();
}

void ModelView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (curveDrawing_ && event->button() == Qt::LeftButton) {
        finishCurveDrawing();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ModelView::mouseMoveEvent(QMouseEvent* event) {
    if (curveDrawing_) {
        drawCurrent_ = event->position();
        update();
        return;
    }
    if (drawing_) {
        drawCurrent_ = event->position();
        update();
        return;
    }
    if (rotating_ && (event->buttons() & Qt::LeftButton)) {
        const QPoint current = event->position().toPoint();
        const QPoint delta = current - lastMousePosition_;
        azimuth_ += delta.x() * 0.01;
        elevation_ = std::clamp(elevation_ + delta.y() * 0.005, -0.8, 1.2);
        lastMousePosition_ = current;
        update();
    }
}

void ModelView::mouseReleaseEvent(QMouseEvent* event) {
    if (curveDrawing_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (drawing_ && event->button() == Qt::LeftButton) {
        drawCurrent_ = event->position();
        drawing_ = false;
        const QPointF start = screenToWorld(drawStart_);
        const QPointF stop = screenToWorld(drawCurrent_);
        if (std::abs(start.x() - stop.x()) > 0.5 &&
            std::abs(start.y() - stop.y()) > 0.5 && rectangleHandler_) {
            rectangleHandler_(std::min(start.x(), stop.x()),
                              std::min(start.y(), stop.y()),
                              std::abs(start.x() - stop.x()),
                              std::abs(start.y() - stop.y()));
        }
        update();
        return;
    }
    rotating_ = false;
    QWidget::mouseReleaseEvent(event);
}

void ModelView::finishCurveDrawing() {
    curveDrawing_ = false;
    setCursor(Qt::ArrowCursor);
    if (curvePoints_.size() >= 2 && curveHandler_) {
        const auto handler = std::move(curveHandler_);
        const auto points = std::move(curvePoints_);
        curveScreenPoints_.clear();
        handler(points);
    } else {
        curveHandler_ = {};
        curvePoints_.clear();
        curveScreenPoints_.clear();
    }
    update();
}
