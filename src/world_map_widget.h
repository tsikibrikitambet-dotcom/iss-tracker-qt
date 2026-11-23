#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QSvgRenderer>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
};

int appendGeoTrackPoint(QVector<QVector<GeoPoint>> &segments, const GeoPoint &point);

class WorldMapWidget final : public QWidget {
    Q_OBJECT

public:
    explicit WorldMapWidget(QWidget *parent = nullptr);

    void setIssPosition(const GeoPoint &position, qint64 timestamp);
    void setHomeLocation(const GeoPoint &position, const QString &name);
    void setTrack(const QVector<QVector<GeoPoint>> &past,
                  const QVector<QVector<GeoPoint>> &projected);
    void pickLocationAt(const QPointF &point);

signals:
    void locationPicked(double latitude, double longitude);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void advanceIssAnimation();
    QRectF worldRect() const;
    QPointF mapPoint(const GeoPoint &point) const;
    void drawTrack(QPainter &painter,
                   const QVector<QVector<GeoPoint>> &segments,
                   const QColor &color,
                   Qt::PenStyle style) const;
    void drawStation(QPainter &painter, const QPointF &center, double headingDegrees) const;
    void drawHome(QPainter &painter, const QPointF &center) const;

    QSvgRenderer mapRenderer_;
    QPixmap mapCache_;
    QSize mapCacheSize_;
    QVector<QVector<GeoPoint>> pastTrack_;
    QVector<QVector<GeoPoint>> projectedTrack_;
    GeoPoint issPosition_;
    GeoPoint animationTarget_;
    GeoPoint homePosition_;
    QString homeName_;
    QTimer animationTimer_;
    QElapsedTimer animationClock_;
    qint64 issTimestamp_ = 0;
    qint64 animationDurationMs_ = 4500;
    double animationStartLatitude_ = 0.0;
    double animationStartLongitude_ = 0.0;
    double animationLongitudeDelta_ = 0.0;
    double issHeadingDegrees_ = 0.0;
    bool hasIssPosition_ = false;
    bool hasHomeLocation_ = false;
};
