#include "PerimeterScanComplexItem.h"

#include "AppSettings.h"
#include "JsonParsing.h"
#include "MissionFlightStatus.h"
#include "PlanMasterController.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "CustomPlugin.h"
#include "CustomSettings.h"
#include "BackendController.h"

#include <QtCore/QJsonArray>

QGC_LOGGING_CATEGORY(PerimeterScanLog, "Custom.PerimeterScan")

PerimeterScanComplexItem::PerimeterScanComplexItem(PlanMasterController *masterController,
                                                   bool flyView,
                                                   const QString &kmlOrShpFile)
    : ComplexMissionItem(masterController, flyView)
{
    _editorQml = QStringLiteral("qrc:/qml/Custom/Plan/PerimeterScanEditor.qml");

    CustomPlugin *customPlugin = qobject_cast<CustomPlugin *>(QGCCorePlugin::instance());
    CustomSettings *customSettings = customPlugin ? customPlugin->customSettings() : nullptr;
    if (customSettings) {
        _sepDistFact = customSettings->separationDistance();
        _emAltOffsetFact = customSettings->emAltOffset();
        _detectorXrayWindowFact = customSettings->detectorXrayWindow();
        _fileNameFact = customSettings->fileName();
        _numImagesFact = customSettings->numImages();
        _overlapFact = customSettings->overlap();
        _bearingFact = customSettings->bearing();
        _swapUavsFact = customSettings->swapUavs();
        _goalLatFact = customSettings->goalLat();
        _goalLonFact = customSettings->goalLon();
        _startLatFact = customSettings->startLat();
        _startLonFact = customSettings->startLon();
        _endLatFact = customSettings->endLat();
        _endLonFact = customSettings->endLon();


        connect(_sepDistFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::_rebuildOffsetPolygon);
        connect(_goalLatFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updateCenterCoordinate);
        connect(_goalLonFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updateCenterCoordinate);
        connect(_startLatFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updatePolyline);
        connect(_startLonFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updatePolyline);
        connect(_endLatFact,   &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updatePolyline);
        connect(_endLonFact,   &Fact::rawValueChanged, this, &PerimeterScanComplexItem::updatePolyline);
        // connect(_swapUavsFact, &Fact::rawValueChanged, this, &PerimeterScanComplexItem::setSwapUavs);
    } else {
        qCWarning(PerimeterScanLog) << "CustomPlugin/CustomSettings not available, PerimeterScan Facts will be null";
    }

    connect(&_corridorPolyline, &QGCMapPolyline::pathChanged, this, &PerimeterScanComplexItem::_setDirty);
    connect(&_corridorPolyline, &QGCMapPolyline::pathChanged, this, &PerimeterScanComplexItem::_polylineChanged);
    connect(&_corridorPolyline, &QGCMapPolyline::pathChanged, this, &PerimeterScanComplexItem::_rebuildOffsetPolygon);
    connect(&_corridorPolyline, &QGCMapPolyline::pathChanged, this, &PerimeterScanComplexItem::updateStartEndCoordinate);

    if (!kmlOrShpFile.isEmpty()) {
        _corridorPolyline.loadKMLOrSHPFile(kmlOrShpFile);
        _corridorPolyline.setDirty(false);
    }

    setDirty(false);
}

void PerimeterScanComplexItem::sendLinearScanGoal() {
    // Invokes the custom plugin's backend controller to send the
    // linear scan target target definition to both drones.

    CustomPlugin* customPlugin = qobject_cast<CustomPlugin*>(QGCCorePlugin::instance());
    qWarning() << "*** sendLinearScanGoal CALLED, customPlugin =" << customPlugin;

    if (_corridorPolyline.count() != 2) {
        qWarning() << "*** sending goal only works with a 2 point line";
    }

    BackendController* backendController = customPlugin ? customPlugin->backendController() : nullptr;
    if (backendController == nullptr) {
        qWarning() << "*** backend is null";
        return;
    }

    QGeoCoordinate startCoord = _corridorPolyline.vertexCoordinate(0);
    QGeoCoordinate endCoord = _corridorPolyline.vertexCoordinate(_corridorPolyline.count() - 1);

    _startLatFact->setRawValue(startCoord.latitude());
    _startLonFact->setRawValue(startCoord.longitude());

    _endLatFact->setRawValue(endCoord.latitude());
    _endLonFact->setRawValue(endCoord.longitude());
    
    // get angle for drone positions
    double start_stop_angle_deg = startCoord.azimuthTo(endCoord); 

    double angle = angleWrap360(start_stop_angle_deg + 90.0);

    if(swapUavs())
    {
        angle = angleWrap360(angle + 180);
    }

    _bearingFact->setRawValue(angle);

    backendController->sendLinearScanGoal();
}

void PerimeterScanComplexItem::setCenterCoordinate(const QGeoCoordinate &coord) {

    if(_center_coordinate != coord)
    {
        _goalLatFact->setRawValue(coord.latitude());
        _goalLonFact->setRawValue(coord.longitude());
        _center_coordinate = coord;
        emit centerCoordinateChanged();
    }

}
void PerimeterScanComplexItem::updateCenterCoordinate() {
    QGeoCoordinate newTarget(_goalLatFact->rawValue().toDouble(),
                              _goalLonFact->rawValue().toDouble());

    if (_center_coordinate != newTarget) {
        _center_coordinate = newTarget;
        emit centerCoordinateChanged();
    }
}

void PerimeterScanComplexItem::updatePolyline()
{
    if (_flyView || _corridorPolyline.count() < 2 || !_startLatFact) {
        return;
    }
    QGeoCoordinate newStart(_startLatFact->rawValue().toDouble(), _startLonFact->rawValue().toDouble());
    QGeoCoordinate newEnd(_endLatFact->rawValue().toDouble(), _endLonFact->rawValue().toDouble());

    if (_corridorPolyline.vertexCoordinate(0) != newStart) {
        _corridorPolyline.adjustVertex(0, newStart);
    }
    if (_corridorPolyline.vertexCoordinate(_corridorPolyline.count() - 1) != newEnd) {
        _corridorPolyline.adjustVertex(_corridorPolyline.count() - 1, newEnd);
    }
}

void PerimeterScanComplexItem::updateStartEndCoordinate() {

    if (_flyView || _corridorPolyline.count() < 2 || !_startLatFact) {
        return;
    }
    QGeoCoordinate start = _corridorPolyline.vertexCoordinate(0);
    QGeoCoordinate end   = _corridorPolyline.vertexCoordinate(_corridorPolyline.count() - 1);

    _startLatFact->setRawValue(start.latitude());
    _startLonFact->setRawValue(start.longitude());
    _endLatFact->setRawValue(end.latitude());
    _endLonFact->setRawValue(end.longitude());

    setCenterCoordinate(lat_lon_midpoint(start, end));
        // get angle for drone positions
    double start_stop_angle_deg = start.azimuthTo(end); 

    double angle = angleWrap360(start_stop_angle_deg + 90.0);

    if(swapUavs())
    {
        angle = angleWrap360(angle + 180);
    }

    _bearingFact->setRawValue(angle);
}

QGeoCoordinate PerimeterScanComplexItem::lat_lon_midpoint(const QGeoCoordinate &a, const QGeoCoordinate &b)
{
    double lat1 = qDegreesToRadians(a.latitude());
    double lon1 = qDegreesToRadians(a.longitude());
    double lat2 = qDegreesToRadians(b.latitude());
    double dLon = qDegreesToRadians(b.longitude() - a.longitude());

    double bx = std::cos(lat2) * std::cos(dLon);
    double by = std::cos(lat2) * std::sin(dLon);

    double latM = std::atan2(std::sin(lat1) + std::sin(lat2),
                              std::sqrt((std::cos(lat1) + bx) * (std::cos(lat1) + bx) + by * by));
    double lonM = lon1 + std::atan2(by, std::cos(lat1) + bx);

    return QGeoCoordinate(qRadiansToDegrees(latM), qRadiansToDegrees(lonM));
}


double PerimeterScanComplexItem::angleWrap360(double angle)
{
    double new_angle = angle;
    while(new_angle > 360.0){
        new_angle -= 360.0;
    }

    while(new_angle < 0.0){
        new_angle += 360.0;
    }

    return new_angle;
}

/*---------------------------------------------------------------------------*/

void PerimeterScanComplexItem::setDirty(bool dirty)
{
    if (_dirty != dirty) {
        _dirty = dirty;
        emit dirtyChanged(_dirty);
    }
}

void PerimeterScanComplexItem::_setDirty()
{
    setDirty(true);
}

void PerimeterScanComplexItem::_polylineChanged()
{
    _recalcScanDistance();
    emit coordinateChanged(coordinate());
    emit specifiesCoordinateChanged();
    emit lastSequenceNumberChanged(lastSequenceNumber());
    emit readyForSaveStateChanged();
}

void PerimeterScanComplexItem::_rebuildOffsetPolygon()
{
    qWarning() << "*** _rebuildOffsetPolygon CALLED, vertex count =" << _corridorPolyline.count();

    if (_corridorPolyline.count() < 2) {
        _offsetPolygon.clear();
        qCDebug(PerimeterScanLog) << "Polyline has fewer than 2 vertices, clearing offset polygon";
        qWarning() << "***Polyline has fewer than 2 vertices";

        return;
    }

    if (sepDist() == nullptr) {
        qCWarning(PerimeterScanLog) << "sepDist Fact is null, cannot rebuild offset polygon";
        return;
    }

    // TODO use the emitter offset to offset the polygon
    auto const halfWidth = sepDist()->rawValue().toDouble() / 2.0;
    qWarning() << "*** _rebuildOffsetPolygon halfWidth =" << halfWidth;

    QList<QGeoCoordinate> firstSideVertices = _corridorPolyline.offsetPolyline(halfWidth);
    QList<QGeoCoordinate> secondSideVertices = _corridorPolyline.offsetPolyline(-halfWidth);

    _offsetPolygon.clear();

    QList<QGeoCoordinate> rgCoord;
    for (const QGeoCoordinate& vertex: firstSideVertices) {
        rgCoord.append(vertex);
    }
    for (int i=secondSideVertices.count() - 1; i >= 0; i--) {
        rgCoord.append(secondSideVertices[i]);
    }
    _offsetPolygon.appendVertices(rgCoord);
    qCDebug(PerimeterScanLog) << "Final offset polygon has" << rgCoord.count() << "vertices:";
    for (int i = 0; i < rgCoord.count(); ++i) {
        qCDebug(PerimeterScanLog) << "  [" << i << "]" << rgCoord[i].toString();
    }
}

void PerimeterScanComplexItem::_recalcScanDistance()
{
    _scanDistance = 0.0;
    // TODO remove or add support for scans
    emit complexDistanceChanged();
}

/*---------------------------------------------------------------------------*/

QGeoCoordinate PerimeterScanComplexItem::coordinate() const
{
    if (_corridorPolyline.count() > 0) {
        return _corridorPolyline.vertexCoordinate(0);
    }
    return {};
}

QGeoCoordinate PerimeterScanComplexItem::exitCoordinate() const
{
    // TODO remove
    return {};
}

double PerimeterScanComplexItem::amslEntryAlt() const
{
    return _altitudeFact.rawValue().toDouble()
           + _missionController->plannedHomePosition().altitude();
}

int PerimeterScanComplexItem::lastSequenceNumber() const
{
    const int n = _corridorPolyline.count();
    if (n < 3) {
        return _sequenceNumber;
    }
    // N vertices + 1 closing waypoint back to first vertex
    return _sequenceNumber + n;
}

VisualMissionItem::ReadyForSaveState PerimeterScanComplexItem::readyForSaveState() const
{
    return _corridorPolyline.isValid() ? ReadyForSave : NotReadyForSaveData;
}

double PerimeterScanComplexItem::greatestDistanceTo(const QGeoCoordinate &other) const
{
    double maxDist = 0;
    (void) other;
    // TODO remove
    return maxDist;
}

/*---------------------------------------------------------------------------*/

void PerimeterScanComplexItem::setSequenceNumber(int sequenceNumber)
{
    if (_sequenceNumber != sequenceNumber) {
        _sequenceNumber = sequenceNumber;
        emit sequenceNumberChanged(sequenceNumber);
        emit lastSequenceNumberChanged(lastSequenceNumber());
    }
}

void PerimeterScanComplexItem::setSwapUavs(const bool swap)
{
    if(_swap_uavs != swap){
        _swap_uavs = swap;
        
        emit swapUavsChanged();
        _swapUavsFact->setRawValue(swap);
    }
}

void PerimeterScanComplexItem::setCoordinate(const QGeoCoordinate & /* coord */)
{
    // Complex items do not support repositioning via a single coordinate.
}

void PerimeterScanComplexItem::applyNewAltitude(double newAltitude)
{
    _altitudeFact.setRawValue(newAltitude);
}

void PerimeterScanComplexItem::setMissionFlightStatus(MissionFlightStatus_t &missionFlightStatus)
{
    ComplexMissionItem::setMissionFlightStatus(missionFlightStatus);
}

/*---------------------------------------------------------------------------*/

void PerimeterScanComplexItem::appendMissionItems(QList<MissionItem *> &items,
                                                  QObject *missionItemParent)
{
    (void) items; // TODO
    (void) missionItemParent; // TODO
    // int    seqNum   = _sequenceNumber;
    // // double altitude = _altitudeFact.rawValue().toDouble();
    // const int count = _perimeterPolygon.count();

    // // One waypoint per polygon vertex.
    // for (int i = 0; i < count; ++i) {
    //     const QGeoCoordinate coord = _perimeterPolygon.vertexCoordinate(i);
    //     items.append(new MissionItem(
    //         seqNum++,
    //         MAV_CMD_NAV_WAYPOINT,
    //         MAV_FRAME_GLOBAL_RELATIVE_ALT,
    //         0,                                            // hold time
    //         0.0,                                          // acceptance radius
    //         0.0,                                          // pass-through
    //         std::numeric_limits<double>::quiet_NaN(),     // yaw – unchanged
    //         coord.latitude(),
    //         coord.longitude(),
    //         altitude,
    //         true,   // autoContinue
    //         false,  // isCurrentItem
    //         missionItemParent));
    // }

    // // Closing waypoint – return to first vertex to complete the loop.
    // if (count >= 3) {
    //     const QGeoCoordinate first = _perimeterPolygon.vertexCoordinate(0);
    //     items.append(new MissionItem(
    //         seqNum++,
    //         MAV_CMD_NAV_WAYPOINT,
    //         MAV_FRAME_GLOBAL_RELATIVE_ALT,
    //         0, 0.0, 0.0,
    //         std::numeric_limits<double>::quiet_NaN(),
    //         first.latitude(),
    //         first.longitude(),
    //         altitude,
    //         true, false,
    //         missionItemParent));
    // }
}

/*---------------------------------------------------------------------------*/

void PerimeterScanComplexItem::save(QJsonArray &missionItems)
{
    QJsonObject saveObject;
    saveObject[JsonParsing::jsonVersionKey]                 = 1;
    saveObject[VisualMissionItem::jsonTypeKey]              = VisualMissionItem::jsonTypeComplexItemValue;
    saveObject[ComplexMissionItem::jsonComplexItemTypeKey]  = jsonComplexItemTypeValue;
    // saveObject[_jsonAltitudeKey]                           = _altitudeFact.rawValue().toDouble();
    // _perimeterPolygon.saveToJson(saveObject);
    missionItems.append(saveObject);
}

bool PerimeterScanComplexItem::load(const QJsonObject &complexObject,
                                    int sequenceNumber,
                                    QString &errorString)
{
    const QList<JsonParsing::KeyValidateInfo> keyInfoList = {
        { JsonParsing::jsonVersionKey,                  QJsonValue::Double, true },
        { VisualMissionItem::jsonTypeKey,               QJsonValue::String, true },
        { ComplexMissionItem::jsonComplexItemTypeKey,   QJsonValue::String, true },
        // { _jsonAltitudeKey,                             QJsonValue::Double, true },
        { QGCMapPolygon::jsonPolygonKey,                QJsonValue::Array,  true },
    };

    if (!JsonParsing::validateKeys(complexObject, keyInfoList, errorString)) {
        return false;
    }

    const QString itemType    = complexObject[VisualMissionItem::jsonTypeKey].toString();
    const QString complexType = complexObject[ComplexMissionItem::jsonComplexItemTypeKey].toString();
    if (itemType != VisualMissionItem::jsonTypeComplexItemValue
            || complexType != jsonComplexItemTypeValue) {
        errorString = tr("%1 does not support loading this complex mission item type: %2:%3")
                          .arg(qgcApp()->applicationName(), itemType, complexType);
        return false;
    }

    const int version = complexObject[JsonParsing::jsonVersionKey].toInt();
    if (version != 1) {
        errorString = tr("%1 version %2 not supported").arg(jsonComplexItemTypeValue).arg(version);
        return false;
    }

    setSequenceNumber(sequenceNumber);
    _corridorPolyline.clear();

    if (!_corridorPolyline.loadFromJson(complexObject, true /* required */, errorString)) {
        return false;
    }

    setDirty(false);
    return true;
}
