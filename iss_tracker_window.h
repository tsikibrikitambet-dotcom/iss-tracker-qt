#pragma once

#include "world_map_widget.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSettings>
#include <QTimer>
#include <QVector>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QNetworkReply;
class AnalogCompassWidget;

struct SatellitePosition {
    GeoPoint ground;
    double altitude = 0.0;
    double velocity = 0.0;
    double solarLatitude = 0.0;
    double solarLongitude = 0.0;
    qint64 timestamp = 0;
    QString visibility;
};

struct VisibilityPass {
    qint64 start = 0;
    qint64 end = 0;
    qint64 peak = 0;
    double maxElevation = 0.0;
};

class IssTrackerWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit IssTrackerWindow(QWidget *parent = nullptr);

private:
    using PositionsCallback =
        std::function<void(const QVector<SatellitePosition> &, const QString &)>;

    void buildUi();
    void restoreCachedState();
    void requestCurrentPosition();
    void loadRecentOrbit();
    void launchOrbitBatches(int generation);
    void finishRecentOrbit(int generation);
    void fetchPositions(const QList<qint64> &timestamps, PositionsCallback callback);
    bool parsePosition(const QJsonObject &object, SatellitePosition *position) const;
    QVector<VisibilityPass> visiblePasses(const QVector<SatellitePosition> &positions) const;
    double elevationDegrees(const SatellitePosition &position) const;
    bool isVisible(const SatellitePosition &position, double *elevation = nullptr) const;
    void updatePassForecast();
    void launchForecastBatches(int generation);
    void finishCoarseForecast(int generation);
    void finishPassForecast(const QVector<VisibilityPass> &passes);
    void renderPasses(const QVector<VisibilityPass> &passes, const QString &status);
    void searchForPlace();
    void setHomeLocation(double latitude, double longitude, const QString &name,
                         const QString &message);
    void addTrackPoint(const GeoPoint &point);
    void setProjectedTrack(const QVector<SatellitePosition> &positions);
    void updateUtcClock();
    void updateOverflightCountdown();
    void updateIssHeading();
    void updatePositionLabels(const SatellitePosition &position, bool cached);
    void saveTrackCache();
    void showNetworkError(const QString &message);
    QString formatLocalTime(qint64 timestamp) const;

    QNetworkAccessManager network_;
    QSettings settings_;
    QTimer telemetryTimer_;
    QTimer orbitTimer_;
    QTimer forecastTimer_;
    QTimer utcTimer_;

    WorldMapWidget *mapWidget_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLabel *utcClockLabel_ = nullptr;
    AnalogCompassWidget *compassWidget_ = nullptr;
    QLabel *updatedLabel_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QLabel *altitudeValue_ = nullptr;
    QLabel *coordinatesLabel_ = nullptr;
    QLabel *locationStatus_ = nullptr;
    QLabel *passStatus_ = nullptr;
    QLineEdit *placeInput_ = nullptr;
    QPushButton *findPlaceButton_ = nullptr;
    QListWidget *passList_ = nullptr;
    qint64 nextOverflightStart_ = 0;
    qint64 nextOverflightEnd_ = 0;

    QVector<QVector<GeoPoint>> pastTrack_{{}};
    QVector<QVector<GeoPoint>> projectedTrack_{{}};
    QVector<SatellitePosition> futureOrbitPositions_;
    QVector<SatellitePosition> forecastPositions_;
    QVector<VisibilityPass> coarsePasses_;
    QVector<VisibilityPass> refinedPasses_;
    QQueue<QList<qint64>> forecastBatches_;
    QQueue<QList<qint64>> orbitBatches_;
    QVector<SatellitePosition> orbitPositions_;
    SatellitePosition currentPosition_;
    GeoPoint homeLocation_;
    QString homeName_;
    QString forecastError_;
    QString orbitError_;
    int trackPointCount_ = 0;
    int orbitGeneration_ = 0;
    int orbitRequestsInFlight_ = 0;
    int forecastGeneration_ = 0;
    int forecastRequestsInFlight_ = 0;
    int refinementsInFlight_ = 0;
    qint64 lastTrackSaveMs_ = 0;
    qint64 orbitReferenceTime_ = 0;
    double issHeadingDegrees_ = 0.0;
    bool hasCurrentPosition_ = false;
    bool hasHomeLocation_ = false;
    bool orbitBatchFailed_ = false;
    bool forecastBatchFailed_ = false;
};
