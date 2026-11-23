#include "world_map_widget.h"

#include <cassert>
#include <cmath>

int main()
{
    QVector<QVector<GeoPoint>> segments;
    segments.append(QVector<GeoPoint>{});

    assert(appendGeoTrackPoint(segments, {60.0, -179.0}) == 1);
    assert(appendGeoTrackPoint(segments, {60.0, -178.0}) == 1);
    assert(segments.size() == 1);
    assert(segments.front().size() == 2);

    QVector<QVector<GeoPoint>> seamSegments;
    seamSegments.append(QVector<GeoPoint>{});
    assert(appendGeoTrackPoint(seamSegments, {10.0, 179.0}) == 1);
    appendGeoTrackPoint(seamSegments, {10.0, -179.0});
    assert(seamSegments.size() == 2);

    QVector<QVector<GeoPoint>> invalidSegments;
    invalidSegments.append(QVector<GeoPoint>{});
    assert(appendGeoTrackPoint(invalidSegments, {std::nan(""), 0.0}) == 0);
    return 0;
}
