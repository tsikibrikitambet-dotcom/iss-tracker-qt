#include "iss_tracker_window.h"

#include <QColor>
#include <QDebug>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QElapsedTimer>
#include <QStackedLayout>
#include <QSslError>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr auto kIssEndpoint = "https://api.wheretheiss.at/v1/satellites/25544";
constexpr auto kPlaceEndpoint = "https://nominatim.openstreetmap.org/search";
constexpr qint64 kTelemetryRefreshMs = 5000;
constexpr qint64 kForecastRefreshMs = 30 * 60 * 1000;
constexpr int kMaxTrackPoints = 1080;
constexpr int kForecastHours = 24;
constexpr int kForecastStepSeconds = 300;
constexpr int kMaxForecastBatch = 20;

const QLocale estonianLocale(QLocale::Estonian, QLocale::Estonia);

void configureSafeNetworkReply(QNetworkReply *reply, qsizetype readBufferSize)
{
    reply->setReadBufferSize(readBufferSize);
    QObject::connect(reply, &QNetworkReply::sslErrors,
                     reply, [reply](const QList<QSslError> &) {
        reply->abort();
    });
    auto *timeout = new QTimer(reply);
    timeout->setTimerType(Qt::PreciseTimer);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, reply, [reply] {
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, timeout, &QTimer::stop);
    timeout->start(15000);
}

QFrame *makePanel(const QString &title, QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("terminalPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(7);

    auto *heading = new QLabel(title, panel);
    heading->setObjectName(QStringLiteral("panelHeading"));
    layout->addWidget(heading);
    return panel;
}

QLabel *makeMetric(const QString &title, const QString &unit, QWidget *parent,
                   QGridLayout *grid, int row, int column, QLabel **value)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("metricCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(9, 8, 9, 8);
    layout->setSpacing(3);

    auto *label = new QLabel(title, card);
    label->setObjectName(QStringLiteral("metricTitle"));
    *value = new QLabel(QStringLiteral("--"), card);
    (*value)->setObjectName(QStringLiteral("metricValue"));
    auto *unitLabel = new QLabel(unit, card);
    unitLabel->setObjectName(QStringLiteral("metricUnit"));
    layout->addWidget(label);
    layout->addWidget(*value);
    layout->addWidget(unitLabel);
    grid->addWidget(card, row, column);
    return label;
}

QJsonObject positionToJson(const SatellitePosition &position)
{
    return {
        {QStringLiteral("latitude"), position.ground.latitude},
        {QStringLiteral("longitude"), position.ground.longitude},
        {QStringLiteral("altitude"), position.altitude},
        {QStringLiteral("velocity"), position.velocity},
        {QStringLiteral("solar_lat"), position.solarLatitude},
        {QStringLiteral("solar_lon"), position.solarLongitude},
        {QStringLiteral("timestamp"), static_cast<double>(position.timestamp)},
        {QStringLiteral("visibility"), position.visibility}
    };
}

class MapClickOverlay final : public QWidget {
public:
    MapClickOverlay(WorldMapWidget *mapWidget, QWidget *parent)
        : QWidget(parent), mapWidget_(mapWidget)
    {
        setObjectName(QStringLiteral("mapClickOverlay"));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            childAt(event->position().toPoint()) == nullptr) {
            mapWidget_->pickLocationAt(event->position());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    WorldMapWidget *mapWidget_;
};
}

class AnalogCompassWidget final : public QWidget {
public:
    explicit AnalogCompassWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(120, 120);
        animationTimer_.setInterval(16);
        animationTimer_.setTimerType(Qt::PreciseTimer);
        connect(&animationTimer_, &QTimer::timeout, this, [this] {
            const double progress = std::clamp(
                animationClock_.elapsed() / 650.0, 0.0, 1.0);
            const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
            displayHeading_ = std::remainder(
                animationStartHeading_ + headingDelta_ * eased, 360.0);
            if (displayHeading_ < 0.0) displayHeading_ += 360.0;
            if (progress >= 1.0) {
                displayHeading_ = targetHeading_;
                animationTimer_.stop();
            }
            update();
        });
    }

    void setHeading(double degrees)
    {
        targetHeading_ = std::fmod(degrees + 360.0, 360.0);
        headingDelta_ = std::remainder(targetHeading_ - displayHeading_, 360.0);
        if (std::abs(headingDelta_) < 0.1) {
            displayHeading_ = targetHeading_;
            update();
            return;
        }
        animationStartHeading_ = displayHeading_;
        animationClock_.start();
        animationTimer_.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::transparent);

        const QRectF dial = rect().adjusted(12.0, 12.0, -12.0, -12.0);
        const QPointF center = dial.center();
        const double radius = dial.width() * 0.5;

        QRadialGradient shadow(center, radius + 8.0);
        shadow.setColorAt(0.0, QColor(7, 9, 7));
        shadow.setColorAt(0.82, QColor(14, 18, 13));
        shadow.setColorAt(1.0, QColor(1, 3, 2));
        painter.setPen(QPen(QColor(85, 105, 76), 2.0));
        painter.setBrush(shadow);
        painter.drawEllipse(dial);
        painter.setPen(QPen(QColor(211, 168, 76, 150), 1.0));
        painter.drawEllipse(dial.adjusted(5.0, 5.0, -5.0, -5.0));
        painter.setPen(QPen(QColor(38, 49, 35), 1.0));
        painter.drawEllipse(dial.adjusted(9.0, 9.0, -9.0, -9.0));

        painter.setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 9, QFont::Bold));
        painter.setPen(QColor(211, 168, 76));
        painter.drawText(QRectF(center.x() - 8, dial.top() - 9, 16, 18), Qt::AlignCenter, tr("N"));
        painter.drawText(QRectF(dial.right() - 11, center.y() - 9, 22, 18), Qt::AlignCenter, tr("E"));
        painter.drawText(QRectF(center.x() - 8, dial.bottom() - 9, 16, 18), Qt::AlignCenter, tr("S"));
        painter.drawText(QRectF(dial.left() - 11, center.y() - 9, 22, 18), Qt::AlignCenter, tr("W"));

        for (int degree = 0; degree < 360; degree += 5) {
            const double angle = degree * M_PI / 180.0 - M_PI / 2.0;
            const bool major = degree % 30 == 0;
            const bool medium = degree % 10 == 0;
            const double inner = major ? 0.66 : (medium ? 0.76 : 0.84);
            painter.setPen(QPen(major ? QColor(211, 168, 76)
                                      : (medium ? QColor(168, 183, 162)
                                                : QColor(95, 112, 88)),
                                major ? 2.0 : 1.0));
            painter.drawLine(
                center + QPointF(std::cos(angle) * radius * inner,
                                 std::sin(angle) * radius * inner),
                center + QPointF(std::cos(angle) * radius * 0.90,
                                 std::sin(angle) * radius * 0.90));
        }

        painter.save();
        painter.translate(center);
        painter.rotate(displayHeading_);
        painter.setPen(QPen(QColor(10, 12, 10), 4.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, 4), QPointF(0, -radius * 0.66));
        painter.setPen(QPen(QColor(143, 47, 38), 1.8));
        painter.setBrush(QColor(181, 67, 55));
        QPainterPath arrow;
        arrow.moveTo(0, -radius * 0.70);
        arrow.lineTo(-7, -radius * 0.38);
        arrow.lineTo(0, -radius * 0.46);
        arrow.lineTo(7, -radius * 0.38);
        arrow.closeSubpath();
        painter.drawPath(arrow);
        painter.setPen(QPen(QColor(210, 210, 190), 1.2));
        painter.setBrush(QColor(215, 215, 195));
        QPainterPath tail;
        tail.moveTo(0, radius * 0.58);
        tail.lineTo(-5, radius * 0.25);
        tail.lineTo(5, radius * 0.25);
        tail.closeSubpath();
        painter.drawPath(tail);
        painter.restore();

        painter.setPen(QPen(QColor(211, 168, 76, 100), 1.0));
        painter.drawEllipse(center, 4.5, 4.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(211, 168, 76));
        painter.drawEllipse(center, 2.0, 2.0);

        painter.setPen(QColor(211, 168, 76));
        painter.setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 7, QFont::Bold));
        painter.drawText(dial.adjusted(0, dial.height() - 10, 0, 2),
                         Qt::AlignCenter,
                         QString::number(static_cast<int>(std::fmod(displayHeading_ + 360.0, 360.0))) +
                         QStringLiteral("°"));
    }

private:
    QTimer animationTimer_;
    QElapsedTimer animationClock_;
    double displayHeading_ = 0.0;
    double targetHeading_ = 0.0;
    double animationStartHeading_ = 0.0;
    double headingDelta_ = 0.0;
};

IssTrackerWindow::IssTrackerWindow(QWidget *parent)
    : QMainWindow(parent),
      network_(this),
      settings_()
{
    setWindowTitle(tr("ISS_TRACK.EXE - Orbital Monitor"));
    setFixedSize(960, 600);
    setStyleSheet(QStringLiteral(R"(
      QWidget {
        background: #050806;
        color: #a8b7a2;
        font-family: "DejaVu Sans Mono", "Courier New", monospace;
        font-size: 9pt;
      }
      QWidget#mapClickOverlay, QLabel { background: transparent; }
      QMainWindow { border: 1px solid #4b5a43; }
      QFrame#terminalPanel, QFrame#metricCard {
        background: transparent;
        border: none;
      }
      QFrame#overflightPanel {
        background: rgba(8, 10, 8, 220);
        border: none;
        border-bottom: 2px solid #8f2f26;
      }
      QLabel#mainTitle, QLabel#metricValue { color: #d3a84c; }
      QLabel#mainTitle { font-size: 15pt; font-weight: bold; letter-spacing: 3px; }
      QLabel#metricTitle, QLabel#metricUnit, QLabel#secondaryText {
        color: #8fa186;
      }
      QLabel#metricTitle, QLabel#secondaryText { font-weight: bold; }
      QLabel#secondaryText { font-size: 8pt; }
      QLabel#panelHeading { color: #d3a84c; font-weight: bold; letter-spacing: 1px; }
      QLabel#metricTitle, QLabel#metricUnit { font-size: 8pt; }
      QLabel#metricValue { font-size: 14pt; font-weight: bold; }
      QLabel#connection {
        color: #92ac72;
        font-weight: bold;
      }
      QLabel#connection[offline="true"] { color: #d3a84c; }
      QLabel#utcClock {
        color: #d3a84c;
      }
      QLineEdit {
        background: transparent;
        color: #a8b7a2;
        border: none;
        border-bottom: 1px solid #4b5a43;
        padding: 4px 2px;
        selection-background-color: #4b5a43;
      }
      QListWidget {
        background: transparent;
        color: #a8b7a2;
        border: none;
        padding: 0;
      }
      QPushButton {
        background: transparent;
        color: #92ac72;
        border: none;
        padding: 4px 2px;
        font-weight: bold;
      }
      QPushButton:hover { color: #d3a84c; }
      QPushButton:disabled { color: #66705f; }
      QListWidget::item { padding: 7px 5px; border-bottom: 1px solid #4b5a43; }
    )"));

    buildUi();
    restoreCachedState();

    connect(&telemetryTimer_, &QTimer::timeout, this, &IssTrackerWindow::requestCurrentPosition);
    telemetryTimer_.start(kTelemetryRefreshMs);
    requestCurrentPosition();

    connect(&utcTimer_, &QTimer::timeout, this, &IssTrackerWindow::updateUtcClock);
    utcTimer_.start(1000);
    updateUtcClock();

    connect(&orbitTimer_, &QTimer::timeout, this, &IssTrackerWindow::loadRecentOrbit);
    orbitTimer_.start(kForecastRefreshMs);
    loadRecentOrbit();

    forecastTimer_.setSingleShot(true);
    connect(&forecastTimer_, &QTimer::timeout, this, &IssTrackerWindow::updatePassForecast);

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationHidden || state == Qt::ApplicationInactive) {
            telemetryTimer_.stop();
            orbitTimer_.stop();
            forecastTimer_.stop();
            utcTimer_.stop();
        } else if (state == Qt::ApplicationActive) {
            if (!telemetryTimer_.isActive()) {
                telemetryTimer_.start(static_cast<int>(kTelemetryRefreshMs));
                requestCurrentPosition();
            }
            if (!utcTimer_.isActive()) utcTimer_.start(1000);
            if (!orbitTimer_.isActive()) orbitTimer_.start(kForecastRefreshMs);
        }
    });
}

void IssTrackerWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *stack = new QStackedLayout(central);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);

    mapWidget_ = new WorldMapWidget(central);
    mapWidget_->setObjectName(QStringLiteral("offlineWorldMap"));
    stack->addWidget(mapWidget_);

    auto *overlay = new MapClickOverlay(mapWidget_, central);
    stack->addWidget(overlay);
    stack->setCurrentWidget(overlay);
    auto *overlayLayout = new QGridLayout(overlay);
    overlayLayout->setContentsMargins(12, 10, 12, 10);
    overlayLayout->setHorizontalSpacing(8);
    overlayLayout->setVerticalSpacing(4);
    overlayLayout->setColumnStretch(1, 1);
    overlayLayout->setRowStretch(1, 1);

    auto *headerPanel = new QFrame(overlay);
    headerPanel->setObjectName(QStringLiteral("terminalPanel"));
    headerPanel->setAttribute(Qt::WA_NoMousePropagation);
    headerPanel->setMaximumWidth(330);
    auto *headerLayout = new QVBoxLayout(headerPanel);
    headerLayout->setContentsMargins(10, 8, 10, 8);
    headerLayout->setSpacing(4);
    auto *headerRow = new QHBoxLayout();
    headerRow->addStretch();
    connectionLabel_ = new QLabel(tr("BOOTING..."), headerPanel);
    connectionLabel_->setObjectName(QStringLiteral("connection"));
    connectionLabel_->setProperty("offline", false);
    headerRow->addWidget(connectionLabel_);
    headerLayout->addLayout(headerRow);
    utcClockLabel_ = new QLabel(headerPanel);
    utcClockLabel_->setObjectName(QStringLiteral("utcClock"));
    headerLayout->addWidget(utcClockLabel_);
    overlayLayout->addWidget(headerPanel, 0, 0, Qt::AlignLeft | Qt::AlignTop);

    auto *metricsPanel = new QFrame(overlay);
    metricsPanel->setObjectName(QStringLiteral("terminalPanel"));
    metricsPanel->setAttribute(Qt::WA_NoMousePropagation);
    metricsPanel->setFixedWidth(390);
    auto *metricsGrid = new QGridLayout(metricsPanel);
    metricsGrid->setContentsMargins(5, 5, 5, 5);
    metricsGrid->setSpacing(10);
    makeMetric(tr("VELOCITY"), tr("KM/H"), metricsPanel, metricsGrid, 0, 0, &speedValue_);
    makeMetric(tr("ORBITAL ALTITUDE"), tr("KM ABOVE EARTH"), metricsPanel, metricsGrid, 0, 1, &altitudeValue_);
    auto *compassCard = new QFrame(metricsPanel);
    compassCard->setObjectName(QStringLiteral("metricCard"));
    compassWidget_ = new AnalogCompassWidget(compassCard);
    auto *compassLayout = new QVBoxLayout(compassCard);
    compassLayout->setContentsMargins(5, 5, 5, 5);
    compassLayout->addWidget(compassWidget_, 0, Qt::AlignCenter);
    metricsGrid->addWidget(compassCard, 0, 2);
    overlayLayout->addWidget(metricsPanel, 0, 2, Qt::AlignRight | Qt::AlignTop);

    auto *locationPanel = makePanel(tr("LOCATION"), overlay);
    locationPanel->setAttribute(Qt::WA_NoMousePropagation);
    locationPanel->setFixedWidth(300);
    auto *locationLayout = qobject_cast<QVBoxLayout *>(locationPanel->layout());
    auto *placeRow = new QHBoxLayout();
    placeInput_ = new QLineEdit(locationPanel);
    placeInput_->setPlaceholderText(tr("CITY / PLACE"));
    findPlaceButton_ = new QPushButton(tr("SET"), locationPanel);
    placeRow->addWidget(placeInput_, 1);
    placeRow->addWidget(findPlaceButton_);
    locationLayout->addLayout(placeRow);
    locationStatus_ = new QLabel(tr("MAP / OFFLINE"), locationPanel);
    locationStatus_->setObjectName(QStringLiteral("secondaryText"));
    locationLayout->addWidget(locationStatus_);
    overlayLayout->addWidget(locationPanel, 2, 0, Qt::AlignLeft | Qt::AlignBottom);

    auto *passesPanel = makePanel(tr("NEXT OVERFLIGHT"), overlay);
    passesPanel->setObjectName(QStringLiteral("overflightPanel"));
    passesPanel->setAttribute(Qt::WA_NoMousePropagation);
    passesPanel->setFixedWidth(300);
    auto *passesLayout = qobject_cast<QVBoxLayout *>(passesPanel->layout());
    passStatus_ = new QLabel(tr("SET LOCATION"), passesPanel);
    passStatus_->setObjectName(QStringLiteral("secondaryText"));
    passStatus_->setWordWrap(true);
    passesLayout->addWidget(passStatus_);
    passList_ = new QListWidget(passesPanel);
    passList_->setMaximumHeight(70);
    passList_->setFrameShape(QFrame::NoFrame);
    passList_->hide();
    passesLayout->addWidget(passList_);
    overlayLayout->addWidget(passesPanel, 2, 2, Qt::AlignRight | Qt::AlignBottom);

    setCentralWidget(central);

    connect(findPlaceButton_, &QPushButton::clicked,
            this, &IssTrackerWindow::searchForPlace);
    connect(placeInput_, &QLineEdit::returnPressed,
            this, &IssTrackerWindow::searchForPlace);
    connect(mapWidget_, &WorldMapWidget::locationPicked, this,
            [this](double latitude, double longitude) {
                setHomeLocation(latitude, longitude, tr("LOC"), tr("LOCATION SAVED"));
            });
}

void IssTrackerWindow::restoreCachedState()
{
    const QByteArray positionBytes = settings_.value(QStringLiteral("state/lastPosition")).toByteArray();
    const QJsonDocument positionDocument = QJsonDocument::fromJson(positionBytes);
    if (positionDocument.isObject()) {
        SatellitePosition position;
        if (parsePosition(positionDocument.object(), &position)) {
            currentPosition_ = position;
            hasCurrentPosition_ = true;
            mapWidget_->setIssPosition(position.ground, position.timestamp);
            updatePositionLabels(position, true);
            connectionLabel_->setText(tr("CACHE"));
            connectionLabel_->setProperty("offline", true);
            style()->unpolish(connectionLabel_);
            style()->polish(connectionLabel_);
            telemetryTimer_.setInterval(15000);
        }
    }

    const QJsonDocument trackDocument = QJsonDocument::fromJson(
        settings_.value(QStringLiteral("state/recentTrack")).toByteArray());
    if (trackDocument.isArray()) {
        pastTrack_.clear();
        trackPointCount_ = 0;
        for (const QJsonValue &segmentValue : trackDocument.array()) {
            if (!segmentValue.isArray()) continue;
            QVector<GeoPoint> segment;
            for (const QJsonValue &pointValue : segmentValue.toArray()) {
                if (!pointValue.isArray()) continue;
                const QJsonArray point = pointValue.toArray();
                if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble()) continue;
                const double latitude = point[0].toDouble();
                const double longitude = point[1].toDouble();
                if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
                    latitude < -90.0 || latitude > 90.0 ||
                    longitude < -180.0 || longitude > 180.0) continue;
                segment.append({latitude, longitude});
            }
            if (!segment.isEmpty()) {
                trackPointCount_ += segment.size();
                pastTrack_.append(segment);
            }
        }
        while (trackPointCount_ > kMaxTrackPoints && !pastTrack_.isEmpty()) {
            if (pastTrack_.first().size() > 1) {
                pastTrack_.first().removeFirst();
            } else {
                pastTrack_.removeFirst();
            }
            --trackPointCount_;
        }
        if (pastTrack_.isEmpty()) pastTrack_.append(QVector<GeoPoint>{});
        mapWidget_->setTrack(pastTrack_, projectedTrack_);
    }

    if (settings_.contains(QStringLiteral("location/latitude")) &&
        settings_.contains(QStringLiteral("location/longitude"))) {
        const double latitude = settings_.value(QStringLiteral("location/latitude")).toDouble();
        const double longitude = settings_.value(QStringLiteral("location/longitude")).toDouble();
        const QString name = settings_.value(QStringLiteral("location/name")).toString();
        if (std::isfinite(latitude) && std::isfinite(longitude) &&
            latitude >= -90.0 && latitude <= 90.0 &&
            longitude >= -180.0 && longitude <= 180.0) {
            if (name != tr("COORDINATES") && name != tr("MAP POINT")) {
                placeInput_->setText(name);
            }
            setHomeLocation(latitude, longitude, name, tr("SAVED LOCATION RESTORED."));
        }
    }

    const QByteArray forecastBytes = settings_.value(QStringLiteral("state/passForecast")).toByteArray();
    const QJsonDocument forecastDocument = QJsonDocument::fromJson(forecastBytes);
    if (forecastDocument.isObject() && passList_->count() == 0) {
        const QJsonObject forecast = forecastDocument.object();
        const double latitude = forecast.value(QStringLiteral("latitude")).toDouble();
        const double longitude = forecast.value(QStringLiteral("longitude")).toDouble();
        if (hasHomeLocation_ &&
            std::abs(latitude - homeLocation_.latitude) < 0.00001 &&
            std::abs(longitude - homeLocation_.longitude) < 0.00001) {
            QVector<VisibilityPass> passes;
            for (const QJsonValue &value : forecast.value(QStringLiteral("passes")).toArray()) {
                const QJsonObject item = value.toObject();
                passes.append({
                    static_cast<qint64>(item.value(QStringLiteral("start")).toDouble()),
                    static_cast<qint64>(item.value(QStringLiteral("end")).toDouble()),
                    static_cast<qint64>(item.value(QStringLiteral("peak")).toDouble()),
                    item.value(QStringLiteral("elevation")).toDouble()
                });
            }
            renderPasses(passes, tr("CACHED"));
        }
    }
}

void IssTrackerWindow::requestCurrentPosition()
{
    QNetworkRequest request(QUrl(QString::fromLatin1(kIssEndpoint)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ISS-Tracker-Qt/1.0"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_.get(request);
    configureSafeNetworkReply(reply, 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorText = reply->errorString();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (body.size() > 1024 * 1024) {
            showNetworkError(tr("Telemetry response exceeds the safe size limit."));
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            showNetworkError(errorText);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        SatellitePosition position;
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject() || !parsePosition(document.object(), &position)) {
            showNetworkError(tr("Invalid ISS telemetry response."));
            return;
        }

        if (hasCurrentPosition_) {
            constexpr double rad = M_PI / 180.0;
            const double lat1 = currentPosition_.ground.latitude * rad;
            const double lat2 = position.ground.latitude * rad;
            const double deltaLon =
                (position.ground.longitude - currentPosition_.ground.longitude) * rad;
            const double y = std::sin(deltaLon) * std::cos(lat2);
            const double x =
                std::cos(lat1) * std::sin(lat2) -
                std::sin(lat1) * std::cos(lat2) * std::cos(deltaLon);
            issHeadingDegrees_ = std::remainder(std::atan2(y, x) / rad, 360.0);
            if (issHeadingDegrees_ < 0.0) issHeadingDegrees_ += 360.0;
        }
        currentPosition_ = position;
        hasCurrentPosition_ = true;
        compassWidget_->setHeading(issHeadingDegrees_);
        mapWidget_->setIssPosition(position.ground, position.timestamp);
        addTrackPoint(position.ground);
        setProjectedTrack(futureOrbitPositions_);
        updatePositionLabels(position, false);

        QJsonObject cachedPosition = positionToJson(position);
        cachedPosition.insert(QStringLiteral("savedAt"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        settings_.setValue(QStringLiteral("state/lastPosition"),
                           QJsonDocument(cachedPosition).toJson(QJsonDocument::Compact));
        connectionLabel_->setText(tr("TLS LIVE"));
        connectionLabel_->setProperty("offline", false);
        style()->unpolish(connectionLabel_);
        style()->polish(connectionLabel_);
        telemetryTimer_.setInterval(static_cast<int>(kTelemetryRefreshMs));
    });
}

void IssTrackerWindow::updateUtcClock()
{
    utcClockLabel_->setText(tr("UTC %1").arg(
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    updateOverflightCountdown();
}

void IssTrackerWindow::updateOverflightCountdown()
{
    if (!passStatus_ || nextOverflightStart_ <= 0) return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now < nextOverflightStart_) {
        qint64 remaining = nextOverflightStart_ - now;
        const qint64 hours = remaining / 3600;
        remaining %= 3600;
        const qint64 minutes = remaining / 60;
        const qint64 seconds = remaining % 60;
        passStatus_->setText(tr("AOS  %1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
    } else if (nextOverflightEnd_ > now) {
        const qint64 remaining = nextOverflightEnd_ - now;
        passStatus_->setText(tr("VISIBLE NOW  %1:%2")
            .arg(remaining / 60, 2, 10, QLatin1Char('0'))
            .arg(remaining % 60, 2, 10, QLatin1Char('0')));
    } else {
        nextOverflightStart_ = 0;
        nextOverflightEnd_ = 0;
        passStatus_->setText(tr("NO NEXT OVERFLIGHT"));
    }
}

void IssTrackerWindow::updatePositionLabels(const SatellitePosition &position, bool cached)
{
    Q_UNUSED(cached)
    Q_UNUSED(position)
    speedValue_->setText(estonianLocale.toString(position.velocity, 'f', 0));
    altitudeValue_->setText(estonianLocale.toString(position.altitude, 'f', 1));
}

void IssTrackerWindow::loadRecentOrbit()
{
    constexpr int sampleStepSeconds = 120;
    constexpr int pastTrackSeconds = 90 * 60;
    constexpr int futureTrackSeconds = 30 * 60;
    const int sampleCount =
        (pastTrackSeconds + futureTrackSeconds) / sampleStepSeconds + 1;
    orbitReferenceTime_ = QDateTime::currentSecsSinceEpoch();
    const int generation = ++orbitGeneration_;
    orbitPositions_.clear();
    orbitBatches_.clear();
    orbitRequestsInFlight_ = 0;
    orbitBatchFailed_ = false;
    orbitError_.clear();

    QList<qint64> timestamps;
    timestamps.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index) {
        timestamps.append(orbitReferenceTime_ - pastTrackSeconds +
                          static_cast<qint64>(index) * sampleStepSeconds);
    }
    for (qsizetype offset = 0; offset < timestamps.size(); offset += kMaxForecastBatch) {
        orbitBatches_.enqueue(timestamps.mid(offset, kMaxForecastBatch));
    }
    launchOrbitBatches(generation);
}

void IssTrackerWindow::launchOrbitBatches(int generation)
{
    if (generation != orbitGeneration_) return;
    while (orbitRequestsInFlight_ < 3 && !orbitBatches_.isEmpty()) {
        const QList<qint64> batch = orbitBatches_.dequeue();
        ++orbitRequestsInFlight_;
        fetchPositions(batch, [this, generation](const QVector<SatellitePosition> &positions,
                                                 const QString &error) {
            if (generation != orbitGeneration_) return;
            --orbitRequestsInFlight_;
            if (!error.isEmpty()) {
                orbitBatchFailed_ = true;
                orbitError_ = error;
                orbitBatches_.clear();
            } else {
                orbitPositions_ += positions;
            }

            if (orbitBatches_.isEmpty() && orbitRequestsInFlight_ == 0) {
                if (orbitBatchFailed_) {
                    qWarning("Could not load recent ISS orbit: %s", qPrintable(orbitError_));
                } else {
                    finishRecentOrbit(generation);
                }
                return;
            }
            launchOrbitBatches(generation);
        });
    }
}

void IssTrackerWindow::finishRecentOrbit(int generation)
{
    if (generation != orbitGeneration_) return;
    std::sort(orbitPositions_.begin(), orbitPositions_.end(),
              [](const SatellitePosition &first, const SatellitePosition &second) {
                  return first.timestamp < second.timestamp;
              });

    pastTrack_.clear();
    pastTrack_.append(QVector<GeoPoint>{});
    trackPointCount_ = 0;
    QVector<SatellitePosition> projected;
    for (const SatellitePosition &position : orbitPositions_) {
        if (position.timestamp <= orbitReferenceTime_) addTrackPoint(position.ground);
        else projected.append(position);
    }
    if (hasCurrentPosition_) addTrackPoint(currentPosition_.ground);
    setProjectedTrack(projected);
    saveTrackCache();
}

void IssTrackerWindow::fetchPositions(const QList<qint64> &timestamps, PositionsCallback callback)
{
    QStringList values;
    values.reserve(timestamps.size());
    for (const qint64 timestamp : timestamps) values.append(QString::number(timestamp));

    QUrl url(QString::fromLatin1(kIssEndpoint) + QStringLiteral("/positions"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("timestamps"), values.join(QLatin1Char(',')));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ISS-Tracker-Qt/1.0"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_.get(request);
    configureSafeNetworkReply(reply, 2 * 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorText = reply->errorString();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (body.size() > 2 * 1024 * 1024) {
            callback({}, tr("Orbit response exceeds the safe size limit."));
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            callback({}, errorText);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            callback({}, tr("Invalid orbit-position response."));
            return;
        }

        QVector<SatellitePosition> positions;
        positions.reserve(document.array().size());
        for (const QJsonValue &value : document.array()) {
            SatellitePosition position;
            if (!value.isObject() || !parsePosition(value.toObject(), &position)) {
                callback({}, tr("Invalid position in orbit response."));
                return;
            }
            positions.append(position);
        }
        std::sort(positions.begin(), positions.end(),
                  [](const SatellitePosition &first, const SatellitePosition &second) {
                      return first.timestamp < second.timestamp;
                  });
        callback(positions, {});
    });
}

bool IssTrackerWindow::parsePosition(const QJsonObject &object, SatellitePosition *position) const
{
    const double latitude = object.value(QStringLiteral("latitude")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double longitude = object.value(QStringLiteral("longitude")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double altitude = object.value(QStringLiteral("altitude")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double velocity = object.value(QStringLiteral("velocity")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double timestamp = object.value(QStringLiteral("timestamp")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        !std::isfinite(altitude) || !std::isfinite(velocity) ||
        !std::isfinite(timestamp) || latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0) {
        return false;
    }

    position->ground = {latitude, longitude};
    position->altitude = altitude;
    position->velocity = velocity;
    position->solarLatitude = object.value(QStringLiteral("solar_lat")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    position->solarLongitude = object.value(QStringLiteral("solar_lon")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    position->timestamp = static_cast<qint64>(timestamp);
    position->visibility = object.value(QStringLiteral("visibility")).toString();
    return true;
}

void IssTrackerWindow::addTrackPoint(const GeoPoint &point)
{
    trackPointCount_ += appendGeoTrackPoint(pastTrack_, point);

    while (trackPointCount_ > kMaxTrackPoints && !pastTrack_.isEmpty()) {
        if (pastTrack_.first().size() > 1) pastTrack_.first().removeFirst();
        else pastTrack_.removeFirst();
        --trackPointCount_;
    }

    mapWidget_->setTrack(pastTrack_, projectedTrack_);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastTrackSaveMs_ >= 60000) saveTrackCache();
}

void IssTrackerWindow::setProjectedTrack(const QVector<SatellitePosition> &positions)
{
    futureOrbitPositions_ = positions;
    projectedTrack_.clear();
    projectedTrack_.append(QVector<GeoPoint>{});
    if (hasCurrentPosition_) {
        appendGeoTrackPoint(projectedTrack_, currentPosition_.ground);
    }
    for (const SatellitePosition &position : positions) {
        if (hasCurrentPosition_ && position.timestamp <= currentPosition_.timestamp) {
            continue;
        }
        appendGeoTrackPoint(projectedTrack_, position.ground);
    }
    mapWidget_->setTrack(pastTrack_, projectedTrack_);
}

void IssTrackerWindow::saveTrackCache()
{
    QJsonArray segments;
    for (const QVector<GeoPoint> &segment : pastTrack_) {
        QJsonArray points;
        for (const GeoPoint &point : segment) {
            points.append(QJsonArray{point.latitude, point.longitude});
        }
        segments.append(points);
    }
    settings_.setValue(QStringLiteral("state/recentTrack"),
                       QJsonDocument(segments).toJson(QJsonDocument::Compact));
    lastTrackSaveMs_ = QDateTime::currentMSecsSinceEpoch();
}

void IssTrackerWindow::showNetworkError(const QString &message)
{
    qWarning("ISS telemetry unavailable: %s", qPrintable(message));
    connectionLabel_->setText(hasCurrentPosition_ ? tr("CACHE") : tr("OFFLINE"));
    connectionLabel_->setProperty("offline", true);
    style()->unpolish(connectionLabel_);
    style()->polish(connectionLabel_);
}

double IssTrackerWindow::elevationDegrees(const SatellitePosition &position) const
{
    constexpr double earthRadiusKm = 6371.0;
    constexpr double degreesToRadians = M_PI / 180.0;
    const double observerLatitude = homeLocation_.latitude * degreesToRadians;
    const double satelliteLatitude = position.ground.latitude * degreesToRadians;
    const double longitudeDifference =
        (position.ground.longitude - homeLocation_.longitude) * degreesToRadians;
    const double cosineCentralAngle = std::clamp(
        std::sin(observerLatitude) * std::sin(satelliteLatitude) +
            std::cos(observerLatitude) * std::cos(satelliteLatitude) *
                std::cos(longitudeDifference),
        -1.0, 1.0);
    const double centralAngle = std::acos(cosineCentralAngle);
    return std::atan2(
        std::cos(centralAngle) - earthRadiusKm / (earthRadiusKm + position.altitude),
        std::sin(centralAngle)
    ) / degreesToRadians;
}

bool IssTrackerWindow::isVisible(const SatellitePosition &position, double *elevation) const
{
    const double elevationAngle = elevationDegrees(position);
    if (elevation) *elevation = elevationAngle;
    if (elevationAngle < 10.0 || position.visibility != QStringLiteral("daylight") ||
        !std::isfinite(position.solarLatitude) || !std::isfinite(position.solarLongitude)) {
        return false;
    }

    constexpr double degreesToRadians = M_PI / 180.0;
    const double observerLatitude = homeLocation_.latitude * degreesToRadians;
    const double solarLatitude = position.solarLatitude * degreesToRadians;
    const double solarLongitudeDifference =
        (position.solarLongitude - homeLocation_.longitude) * degreesToRadians;
    const double solarSine = std::clamp(
        std::sin(observerLatitude) * std::sin(solarLatitude) +
            std::cos(observerLatitude) * std::cos(solarLatitude) *
                std::cos(solarLongitudeDifference),
        -1.0, 1.0);
    const double solarElevation =
        std::asin(solarSine) / degreesToRadians;
    return solarElevation <= -6.0;
}

QVector<VisibilityPass> IssTrackerWindow::visiblePasses(
    const QVector<SatellitePosition> &positions) const
{
    QVector<VisibilityPass> passes;
    for (const SatellitePosition &position : positions) {
        double elevation = 0.0;
        if (!isVisible(position, &elevation)) continue;

        if (!passes.isEmpty() && position.timestamp - passes.last().end <= 600) {
            VisibilityPass &pass = passes.last();
            pass.end = position.timestamp;
            if (elevation > pass.maxElevation) {
                pass.maxElevation = elevation;
                pass.peak = position.timestamp;
            }
        } else {
            passes.append({position.timestamp, position.timestamp, position.timestamp, elevation});
        }
    }
    return passes;
}

void IssTrackerWindow::updatePassForecast()
{
    if (!hasHomeLocation_) return;
    const int generation = ++forecastGeneration_;
    const QByteArray cachedBytes = settings_.value(QStringLiteral("state/passForecast")).toByteArray();
    const QJsonDocument cachedDocument = QJsonDocument::fromJson(cachedBytes);
    qint64 cachedAt = 0;
    bool sameLocation = false;
    if (cachedDocument.isObject()) {
        const QJsonObject cached = cachedDocument.object();
        cachedAt = static_cast<qint64>(cached.value(QStringLiteral("savedAt")).toDouble());
        sameLocation =
            std::abs(cached.value(QStringLiteral("latitude")).toDouble() - homeLocation_.latitude) < 0.00001 &&
            std::abs(cached.value(QStringLiteral("longitude")).toDouble() - homeLocation_.longitude) < 0.00001;
        if (sameLocation) {
            QVector<VisibilityPass> passes;
            for (const QJsonValue &value : cached.value(QStringLiteral("passes")).toArray()) {
                const QJsonObject item = value.toObject();
                passes.append({
                    static_cast<qint64>(item.value(QStringLiteral("start")).toDouble()),
                    static_cast<qint64>(item.value(QStringLiteral("end")).toDouble()),
                    static_cast<qint64>(item.value(QStringLiteral("peak")).toDouble()),
                    item.value(QStringLiteral("elevation")).toDouble()
                });
            }
            renderPasses(passes, tr("CACHED FORECAST / %1").arg(
                QDateTime::fromMSecsSinceEpoch(cachedAt).toLocalTime().toString(QStringLiteral("HH:mm"))));
        }
    }

    if (sameLocation && cachedAt > 0 &&
        QDateTime::currentMSecsSinceEpoch() - cachedAt < kForecastRefreshMs) {
        forecastTimer_.start(static_cast<int>(
            kForecastRefreshMs - (QDateTime::currentMSecsSinceEpoch() - cachedAt)));
        return;
    }

    if (!sameLocation) {
        passList_->clear();
        passList_->hide();
    }
    passStatus_->setText(tr("SCANNING NEXT 24 HOURS FOR VISIBLE PASSES..."));
    forecastBatches_.clear();
    forecastPositions_.clear();
    forecastRequestsInFlight_ = 0;
    forecastBatchFailed_ = false;
    forecastError_.clear();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 first = ((now + kForecastStepSeconds - 1) / kForecastStepSeconds) *
                         kForecastStepSeconds;
    const int count = kForecastHours * 3600 / kForecastStepSeconds;
    QList<qint64> timestamps;
    timestamps.reserve(count);
    for (int index = 0; index < count; ++index) {
        timestamps.append(first + static_cast<qint64>(index) * kForecastStepSeconds);
    }
    for (int index = 0; index < timestamps.size(); index += kMaxForecastBatch) {
        forecastBatches_.enqueue(timestamps.mid(index, kMaxForecastBatch));
    }
    launchForecastBatches(generation);
}

void IssTrackerWindow::launchForecastBatches(int generation)
{
    if (generation != forecastGeneration_) return;
    while (forecastRequestsInFlight_ < 4 && !forecastBatches_.isEmpty()) {
        const QList<qint64> batch = forecastBatches_.dequeue();
        ++forecastRequestsInFlight_;
        fetchPositions(batch, [this, generation](const QVector<SatellitePosition> &positions,
                                                 const QString &error) {
            if (generation != forecastGeneration_) return;
            --forecastRequestsInFlight_;
            if (!error.isEmpty()) {
                forecastBatchFailed_ = true;
                forecastError_ = error;
                forecastBatches_.clear();
            } else {
                forecastPositions_ += positions;
            }
            if (forecastBatches_.isEmpty() && forecastRequestsInFlight_ == 0) {
                if (forecastBatchFailed_) {
                    passStatus_->setText(tr("FORECAST UNAVAILABLE: %1").arg(forecastError_));
                    forecastTimer_.start(static_cast<int>(kForecastRefreshMs));
                } else {
                    finishCoarseForecast(generation);
                }
                return;
            }
            launchForecastBatches(generation);
        });
    }
}

void IssTrackerWindow::finishCoarseForecast(int generation)
{
    if (generation != forecastGeneration_) return;
    std::sort(forecastPositions_.begin(), forecastPositions_.end(),
              [](const SatellitePosition &first, const SatellitePosition &second) {
                  return first.timestamp < second.timestamp;
              });
    coarsePasses_ = visiblePasses(forecastPositions_);
    refinedPasses_.clear();
    refinementsInFlight_ = std::min(6, static_cast<int>(coarsePasses_.size()));
    if (refinementsInFlight_ == 0) {
        finishPassForecast({});
        return;
    }

    for (int index = 0; index < refinementsInFlight_; ++index) {
        const VisibilityPass coarse = coarsePasses_[index];
        QList<qint64> timestamps;
        const qint64 first = coarse.peak - 427;
        for (int sample = 0; sample < 20; ++sample) timestamps.append(first + sample * 45);
        fetchPositions(timestamps, [this, generation, coarse](
                                        const QVector<SatellitePosition> &positions,
                                        const QString &error) {
            if (generation != forecastGeneration_) return;
            if (error.isEmpty()) {
                const QVector<VisibilityPass> refined = visiblePasses(positions);
                if (!refined.isEmpty()) {
                    const auto best = std::min_element(
                        refined.cbegin(), refined.cend(),
                        [&coarse](const VisibilityPass &first, const VisibilityPass &second) {
                            return std::abs(first.peak - coarse.peak) <
                                   std::abs(second.peak - coarse.peak);
                        });
                    refinedPasses_.append(*best);
                } else {
                    refinedPasses_.append(coarse);
                }
            } else {
                qWarning("Could not refine ISS pass: %s", qPrintable(error));
                refinedPasses_.append(coarse);
            }
            --refinementsInFlight_;
            if (refinementsInFlight_ == 0) {
                std::sort(refinedPasses_.begin(), refinedPasses_.end(),
                          [](const VisibilityPass &first, const VisibilityPass &second) {
                              return first.start < second.start;
                          });
                finishPassForecast(refinedPasses_);
            }
        });
    }
}

void IssTrackerWindow::finishPassForecast(const QVector<VisibilityPass> &passes)
{
    renderPasses(passes, passes.isEmpty()
        ? tr("NO VISIBLE PASSES PREDICTED IN THE NEXT 24 HOURS.")
        : tr("PASS FORECAST UPDATED / TIMES ARE LOCAL."));
    QJsonArray serialized;
    for (const VisibilityPass &pass : passes) {
        serialized.append(QJsonObject{
            {QStringLiteral("start"), static_cast<double>(pass.start)},
            {QStringLiteral("end"), static_cast<double>(pass.end)},
            {QStringLiteral("peak"), static_cast<double>(pass.peak)},
            {QStringLiteral("elevation"), pass.maxElevation}
        });
    }
    const QJsonObject forecast{
        {QStringLiteral("latitude"), homeLocation_.latitude},
        {QStringLiteral("longitude"), homeLocation_.longitude},
        {QStringLiteral("savedAt"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("passes"), serialized}
    };
    settings_.setValue(QStringLiteral("state/passForecast"),
                       QJsonDocument(forecast).toJson(QJsonDocument::Compact));
    forecastTimer_.start(static_cast<int>(kForecastRefreshMs));
}

void IssTrackerWindow::renderPasses(const QVector<VisibilityPass> &passes,
                                    const QString &status)
{
    nextOverflightStart_ = 0;
    nextOverflightEnd_ = 0;
    passStatus_->setText(status);
    passList_->clear();
    if (passes.isEmpty()) {
        if (status.startsWith(tr("NO VISIBLE PASSES"))) {
            passList_->addItem(tr("NO VISIBLE PASSES IN NEXT 24H"));
            passList_->show();
        }
        return;
    }

    passList_->show();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const VisibilityPass &pass : passes) {
        if (pass.end >= now) {
            nextOverflightStart_ = pass.start;
            nextOverflightEnd_ = pass.end;
            break;
        }
    }
    if (nextOverflightStart_ > 0) updateOverflightCountdown();
    for (int index = 0; index < std::min(4, static_cast<int>(passes.size())); ++index) {
        const VisibilityPass &pass = passes[index];
        const QString timeRange = QStringLiteral("%1 — %2")
            .arg(formatLocalTime(pass.start), formatLocalTime(pass.end));
        auto *item = new QListWidgetItem(
            tr("%1\nPEAK %2° / LOCAL TIME")
                .arg(timeRange)
                .arg(estonianLocale.toString(pass.maxElevation, 'f', 1)),
            passList_);
        item->setForeground(QColor(QStringLiteral("#ffca72")));
    }
}

void IssTrackerWindow::setHomeLocation(double latitude, double longitude,
                                       const QString &name, const QString &message)
{
    Q_UNUSED(message)
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0) {
        locationStatus_->setText(tr("INVALID"));
        return;
    }

    homeLocation_ = {latitude, longitude};
    homeName_ = name.simplified().left(80);
    hasHomeLocation_ = true;
    mapWidget_->setHomeLocation(homeLocation_, homeName_);
    settings_.setValue(QStringLiteral("location/latitude"), latitude);
    settings_.setValue(QStringLiteral("location/longitude"), longitude);
    settings_.setValue(QStringLiteral("location/name"), homeName_);
    settings_.sync();
    locationStatus_->setText(settings_.status() == QSettings::NoError
                                 ? tr("LOCATION SAVED")
                                 : tr("SAVE ERROR"));
    updatePassForecast();
}

QString IssTrackerWindow::formatLocalTime(qint64 timestamp) const
{
    static const QLocale english(QLocale::English);
    return english.toString(
        QDateTime::fromSecsSinceEpoch(timestamp).toLocalTime(),
        QStringLiteral("ddd, MMM d, HH:mm"));
}

void IssTrackerWindow::searchForPlace()
{
    const QString queryText = placeInput_->text().trimmed();
    if (queryText.isEmpty()) {
        locationStatus_->setText(tr("ENTER A CITY OR PLACE NAME."));
        return;
    }

    findPlaceButton_->setEnabled(false);
    locationStatus_->setText(tr("SEARCHING FOR PLACE..."));
    QUrl url(QString::fromLatin1(kPlaceEndpoint));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("jsonv2"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("accept-language"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("q"), queryText);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("ISS-Tracker-Qt/1.0 (Raspberry Pi desktop application)"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_.get(request);
    configureSafeNetworkReply(reply, 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorText = reply->errorString();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        findPlaceButton_->setEnabled(true);

        if (body.size() > 1024 * 1024) {
            locationStatus_->setText(tr("PLACE SEARCH RESPONSE EXCEEDED SAFE LIMIT."));
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            locationStatus_->setText(tr("PLACE SEARCH FAILED: %1").arg(errorText));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            locationStatus_->setText(tr("PLACE SEARCH RETURNED INVALID DATA."));
            return;
        }
        if (document.array().isEmpty()) {
            locationStatus_->setText(tr("PLACE NOT FOUND. TRY A CITY AND COUNTRY."));
            return;
        }

        const QJsonObject result = document.array().first().toObject();
        bool latitudeOk = false;
        bool longitudeOk = false;
        const double latitude = result.value(QStringLiteral("lat")).toString().toDouble(&latitudeOk);
        const double longitude = result.value(QStringLiteral("lon")).toString().toDouble(&longitudeOk);
        const QString name = result.value(QStringLiteral("display_name")).toString();
        if (!latitudeOk || !longitudeOk || name.isEmpty()) {
            locationStatus_->setText(tr("PLACE SEARCH RETURNED INVALID COORDINATES."));
            return;
        }
        setHomeLocation(latitude, longitude, name, tr("LOCATION SAVED: %1").arg(name));
    });
}
