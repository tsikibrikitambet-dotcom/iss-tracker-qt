#include "world_map_widget.h"

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

WorldMapWidget::WorldMapWidget(QWidget *parent)
    : QWidget(parent),
      mapRenderer_(QStringLiteral(":/maps/world-map.svg"))
{
    setMinimumSize(320, 220);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Click the map to set your location"));
    animationTimer_.setInterval(16);
    animationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_, &QTimer::timeout, this, &WorldMapWidget::advanceIssAnimation);
}

int appendGeoTrackPoint(QVector<QVector<GeoPoint>> &segments, const GeoPoint &point)
{
    if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude) ||
        point.latitude < -90.0 || point.latitude > 90.0 ||
        point.longitude < -180.0 || point.longitude > 180.0) {
        return 0;
    }
    if (segments.isEmpty()) segments.append(QVector<GeoPoint>{});
    QVector<GeoPoint> &segment = segments.last();
    if (segment.isEmpty()) {
        segment.append(point);
        return 1;
    }

    const GeoPoint previous = segment.last();
    const double rawLongitudeDelta = point.longitude - previous.longitude;
    const double wrappedLongitudeDelta = std::remainder(rawLongitudeDelta, 360.0);
    if (std::abs(rawLongitudeDelta) > 180.0 &&
        std::abs(wrappedLongitudeDelta) > 0.000001) {
        const double unwrappedLongitude = previous.longitude + wrappedLongitudeDelta;
        const double seamLongitude = unwrappedLongitude > previous.longitude ? 180.0 : -180.0;
        const double fraction =
            (seamLongitude - previous.longitude) / wrappedLongitudeDelta;
        const double seamLatitude = previous.latitude +
            (point.latitude - previous.latitude) * fraction;
        segment.append({seamLatitude, seamLongitude});
        segments.append(QVector<GeoPoint>{{seamLatitude, -seamLongitude}, point});
        return 3;
    }

    segment.append(point);
    return 1;
}

void WorldMapWidget::setIssPosition(const GeoPoint &position, qint64 timestamp)
{
    if (hasIssPosition_ && timestamp > 0 && issTimestamp_ > 0 && timestamp <= issTimestamp_) return;

    if (!hasIssPosition_ || timestamp <= 0 || issTimestamp_ <= 0) {
        issPosition_ = position;
        animationTimer_.stop();
    } else {
        const qint64 elapsedSeconds = timestamp - issTimestamp_;
        const double longitudeDelta =
            std::remainder(position.longitude - issPosition_.longitude, 360.0);
        const double latitudeDelta = position.latitude - issPosition_.latitude;
        if (elapsedSeconds > 30 || std::hypot(longitudeDelta, latitudeDelta) > 5.0) {
            issPosition_ = position;
            animationTimer_.stop();
        } else {
            animationTarget_ = position;
            animationStartLatitude_ = issPosition_.latitude;
            animationStartLongitude_ = issPosition_.longitude;
            animationLongitudeDelta_ = longitudeDelta;
            animationDurationMs_ = std::clamp<qint64>(elapsedSeconds * 1000, 500, 5000);
            if (std::hypot(longitudeDelta, latitudeDelta) > 0.00001) {
                issHeadingDegrees_ =
                    std::atan2(-latitudeDelta, longitudeDelta) * 180.0 / M_PI;
            }
            animationClock_.start();
            animationTimer_.start();
        }
    }

    if (timestamp > 0) issTimestamp_ = timestamp;
    hasIssPosition_ = true;
    update();
}

void WorldMapWidget::advanceIssAnimation()
{
    const double progress = std::clamp(
        animationClock_.elapsed() / static_cast<double>(animationDurationMs_), 0.0, 1.0);
    const double eased = progress;
    issPosition_.latitude = animationStartLatitude_ +
                            (animationTarget_.latitude - animationStartLatitude_) * eased;
    const double unwrappedLongitude =
        animationStartLongitude_ + animationLongitudeDelta_ * eased;
    issPosition_.longitude = std::remainder(unwrappedLongitude, 360.0);

    if (progress >= 1.0) {
        issPosition_ = animationTarget_;
        animationTimer_.stop();
    }
    update();
}

void WorldMapWidget::setHomeLocation(const GeoPoint &position, const QString &name)
{
    homePosition_ = position;
    homeName_ = name;
    hasHomeLocation_ = true;
    update();
}

void WorldMapWidget::setTrack(const QVector<QVector<GeoPoint>> &past,
                              const QVector<QVector<GeoPoint>> &projected)
{
    pastTrack_ = past;
    projectedTrack_ = projected;
    for (auto segment = pastTrack_.crbegin(); segment != pastTrack_.crend(); ++segment) {
        if (segment->size() < 2) continue;
        const GeoPoint &previous = (*segment)[segment->size() - 2];
        const GeoPoint &last = segment->last();
        const double longitudeDelta = std::remainder(last.longitude - previous.longitude, 360.0);
        const double latitudeDelta = last.latitude - previous.latitude;
        if (std::hypot(longitudeDelta, latitudeDelta) > 0.00001) {
            issHeadingDegrees_ = std::atan2(-latitudeDelta, longitudeDelta) * 180.0 / M_PI;
        }
        break;
    }
    update();
}

void WorldMapWidget::pickLocationAt(const QPointF &point)
{
    const QRectF bounds = worldRect();
    if (!bounds.contains(point)) return;
    const double longitude =
        (point.x() - bounds.left()) / bounds.width() * 360.0 - 180.0;
    const double latitude = 90.0 - (point.y() - bounds.top()) / bounds.height() * 180.0;
    emit locationPicked(std::clamp(latitude, -90.0, 90.0),
                        std::clamp(longitude, -180.0, 180.0));
}

QRectF WorldMapWidget::worldRect() const
{
    constexpr double aspectRatio = 2.0;
    const double availableWidth = width();
    const double availableHeight = height();
    double mapWidth = availableWidth;
    double mapHeight = mapWidth / aspectRatio;

    if (mapHeight > availableHeight) {
        mapHeight = availableHeight;
        mapWidth = mapHeight * aspectRatio;
    }

    return QRectF((availableWidth - mapWidth) / 2.0,
                  (availableHeight - mapHeight) / 2.0,
                  mapWidth,
                  mapHeight);
}

QPointF WorldMapWidget::mapPoint(const GeoPoint &point) const
{
    const QRectF bounds = worldRect();
    return QPointF(
        bounds.left() + (point.longitude + 180.0) / 360.0 * bounds.width(),
        bounds.top() + (90.0 - point.latitude) / 180.0 * bounds.height()
    );
}

void WorldMapWidget::drawTrack(QPainter &painter,
                               const QVector<QVector<GeoPoint>> &segments,
                               const QColor &color,
                               Qt::PenStyle style) const
{
    QPen pen(color, style == Qt::SolidLine ? 2.2 : 1.8, style, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    for (const auto &segment : segments) {
        if (segment.size() < 2) continue;
        const QRectF bounds = worldRect();
        QVector<GeoPoint> visibleSegment;
        visibleSegment.append(segment.front());
        const auto drawVisibleSegment = [&](const QVector<GeoPoint> &points) {
            if (points.size() < 2) return;
            QPainterPath path;
            path.moveTo(mapPoint(points.front()));
            if (points.size() == 2) {
                path.lineTo(mapPoint(points[1]));
            } else {
                for (qsizetype index = 0; index + 1 < points.size(); ++index) {
                    const QPointF p0 = mapPoint(points[std::max<qsizetype>(0, index - 1)]);
                    const QPointF p1 = mapPoint(points[index]);
                    const QPointF p2 = mapPoint(points[index + 1]);
                    const QPointF p3 = mapPoint(points[std::min<qsizetype>(points.size() - 1, index + 2)]);
                    const QPointF c1 = p1 + (p2 - p0) / 6.0;
                    const QPointF c2 = p2 - (p3 - p1) / 6.0;
                    path.cubicTo(c1, c2, p2);
                }
            }
            painter.save();
            painter.setClipRect(bounds);
            painter.drawPath(path);
            painter.restore();
        };

        for (qsizetype index = 1; index < segment.size(); ++index) {
            const QPointF previous = mapPoint(segment[index - 1]);
            const QPointF current = mapPoint(segment[index]);
            if (std::abs(current.x() - previous.x()) > bounds.width() * 0.45) {
                drawVisibleSegment(visibleSegment);
                visibleSegment.clear();
                visibleSegment.append(segment[index]);
            } else {
                visibleSegment.append(segment[index]);
            }
        }
        drawVisibleSegment(visibleSegment);
    }
}

void WorldMapWidget::drawStation(QPainter &painter, const QPointF &center,
                                 double headingDegrees) const
{
    painter.save();
    painter.translate(center);
    painter.scale(0.32, 0.32);

    QRadialGradient glow(QPointF(0, 0), 34);
    glow.setColorAt(0.0, QColor(103, 250, 132, 70));
    glow.setColorAt(1.0, QColor(103, 250, 132, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(QPointF(0, 0), 34, 34);

    painter.save();
    painter.rotate(headingDegrees);
    const QColor green(139, 255, 158);
    const QColor darkGreen(3, 25, 10);
    QPen structure(green, 1.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(structure);
    painter.setBrush(darkGreen);

    painter.drawLine(QPointF(-40, 0), QPointF(40, 0));
    painter.drawLine(QPointF(-36, -3), QPointF(36, -3));
    painter.drawLine(QPointF(-36, 3), QPointF(36, 3));

    const QRectF arrays[] = {
        QRectF(-29, -43, 24, 28), QRectF(5, -43, 24, 28),
        QRectF(-29, 15, 24, 28), QRectF(5, 15, 24, 28)
    };
    for (const QRectF &array : arrays) {
        painter.drawRect(array);
        for (int column = 1; column < 4; ++column) {
            const double x = array.left() + array.width() * column / 4.0;
            painter.drawLine(QPointF(x, array.top()), QPointF(x, array.bottom()));
        }
        painter.drawLine(QPointF(array.left(), array.center().y()),
                         QPointF(array.right(), array.center().y()));
    }

    painter.drawLine(QPointF(-17, -15), QPointF(-17, 15));
    painter.drawLine(QPointF(17, -15), QPointF(17, 15));

    painter.setPen(QPen(green, 1.4));
    painter.setBrush(QColor(160, 255, 178));
    painter.drawRoundedRect(QRectF(-15, -7, 12, 14), 3, 3);
    painter.drawRoundedRect(QRectF(-2, -8, 12, 16), 3, 3);
    painter.drawRoundedRect(QRectF(11, -6, 10, 12), 3, 3);
    painter.setBrush(darkGreen);
    painter.drawEllipse(QRectF(5, -3, 4, 6));
    painter.drawLine(QPointF(-3, -6), QPointF(-3, 6));

    painter.setPen(QPen(green, 1.5));
    painter.drawLine(QPointF(25, -5), QPointF(31, -11));
    painter.drawEllipse(QRectF(28, -16, 8, 8));
    painter.drawLine(QPointF(-24, -4), QPointF(-29, -11));
    painter.drawEllipse(QRectF(-34, -16, 8, 8));

    painter.setPen(QPen(QColor(103, 250, 132), 4.0));
    painter.drawLine(QPointF(-46, 0), QPointF(46, 0));

    painter.restore();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 202, 114));
    painter.drawEllipse(QPointF(0, 0), 2.5, 2.5);
    painter.restore();
}

void WorldMapWidget::drawHome(QPainter &painter, const QPointF &center) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 202, 114), 1.8));
    painter.setBrush(QColor(2, 7, 4));
    painter.drawEllipse(center, 7, 7);
    painter.setBrush(QColor(255, 202, 114));
    painter.drawEllipse(center, 2.5, 2.5);
    painter.drawLine(center + QPointF(-12, 0), center + QPointF(-5, 0));
    painter.drawLine(center + QPointF(5, 0), center + QPointF(12, 0));
    painter.drawLine(center + QPointF(0, -12), center + QPointF(0, -5));
    painter.drawLine(center + QPointF(0, 5), center + QPointF(0, 12));
    const QFont labelFont(QStringLiteral("Courier New"), 8, QFont::Bold);
    painter.setFont(labelFont);
    painter.setPen(QColor(255, 202, 114));
    const QString label = homeName_.isEmpty() ? QStringLiteral("YOU") : homeName_;
    const QFontMetrics metrics(labelFont);
    const QRectF bounds = worldRect();
    const double labelWidth = metrics.horizontalAdvance(label);
    const double labelX = center.x() + 10.0 + labelWidth <= bounds.right()
                              ? center.x() + 10.0
                              : center.x() - labelWidth - 10.0;
    const double labelY = center.y() - 9.0 < bounds.top() + metrics.ascent()
                              ? center.y() + metrics.ascent() + 7.0
                              : center.y() - 9.0;
    painter.drawText(QPointF(labelX, labelY), label);
    painter.restore();
}

void WorldMapWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(QStringLiteral("#080a08")));
    const QRectF bounds = worldRect();

    if (mapRenderer_.isValid()) {
        const QSize cacheSize = bounds.size().toSize();
        if (mapCache_.isNull() || mapCacheSize_ != cacheSize) {
            mapCache_ = QPixmap(cacheSize);
            mapCache_.fill(QColor(QStringLiteral("#080a08")));
            QPainter cachePainter(&mapCache_);
            cachePainter.setRenderHint(QPainter::Antialiasing);
            mapRenderer_.render(&cachePainter, QRectF(0.0, 0.0,
                                                      cacheSize.width(),
                                                      cacheSize.height()));
            mapCacheSize_ = cacheSize;
        }
        painter.drawPixmap(bounds.topLeft(), mapCache_);
    }

    painter.save();
    painter.setClipRect(bounds);
    painter.setPen(QPen(QColor(190, 160, 90, 55), 0.7, Qt::DotLine));
    for (int latitude = -60; latitude <= 60; latitude += 30) {
        const double y = bounds.top() + (90.0 - latitude) / 180.0 * bounds.height();
        painter.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
    }
    for (int longitude = -150; longitude <= 150; longitude += 30) {
        const double x = bounds.left() + (longitude + 180.0) / 360.0 * bounds.width();
        painter.drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
    }
    painter.setPen(QPen(QColor(211, 168, 76, 18), 1));
    for (double y = bounds.top(); y < bounds.bottom(); y += 4.0) {
        painter.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
    }
    painter.restore();

    const QPointF issPoint = mapPoint(issPosition_);
    const auto joinSegments = [](const QVector<QVector<GeoPoint>> &segments) {
        QVector<QVector<GeoPoint>> joined;
        for (const QVector<GeoPoint> &segment : segments) {
            if (segment.isEmpty()) continue;
            if (!joined.isEmpty() && !joined.last().isEmpty() &&
                std::abs(joined.last().last().latitude -
                         segment.front().latitude) < 0.000001 &&
                std::abs(std::remainder(joined.last().last().longitude -
                                        segment.front().longitude, 360.0)) < 0.000001) {
                for (qsizetype index = 1; index < segment.size(); ++index) {
                    joined.last().append(segment[index]);
                }
            } else {
                joined.append(segment);
            }
        }
        return joined;
    };
    const auto pastTrack = joinSegments(pastTrack_);
    const auto projectedTrack = joinSegments(projectedTrack_);
    drawTrack(painter, pastTrack, QColor(180, 55, 48), Qt::SolidLine);
    drawTrack(painter, projectedTrack, QColor(255, 202, 114), Qt::SolidLine);
    if (hasIssPosition_) drawStation(painter, issPoint, issHeadingDegrees_);

    if (hasIssPosition_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 202, 114));
        painter.drawEllipse(issPoint, 2.0, 2.0);
    }
    if (hasHomeLocation_) drawHome(painter, mapPoint(homePosition_));
}

void WorldMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    pickLocationAt(event->position());
}
