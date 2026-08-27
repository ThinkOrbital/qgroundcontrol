#pragma once

#include <limits>

#include "ComplexMissionItem.h"
#include "FactMetaData.h"
#include "MissionItem.h"
#include "QGCMapPolygon.h"
#include "QGCMapPolyline.h"
#include "SettingsFact.h"

class PlanMasterController;

/// \brief A custom complex mission item that generates waypoints following the perimeter of a user-defined polygon.
///
/// A custom complex mission item that generates waypoints following the
/// perimeter of a user-defined polygon. Shows full polygon-editing tools
/// on the map (same toolbar as Survey/StructureScan).

class PerimeterScanComplexItem : public ComplexMissionItem
{
    Q_OBJECT

public:
    /// @param kmlOrShpFile  Optional KML/SHP file to seed the polygon from; empty for blank.
    explicit PerimeterScanComplexItem(PlanMasterController *masterController,
                                      bool flyView,
                                      const QString &kmlOrShpFile = QString());

    // Q_PROPERTY(QGCMapPolygon *perimeterPolygon READ perimeterPolygon CONSTANT) // TODO remove
    Q_PROPERTY(QGCMapPolygon *offsetPolygon READ offsetPolygon CONSTANT)
    Q_PROPERTY(QGCMapPolyline *corridorPolyline READ corridorPolyline CONSTANT)
    Q_PROPERTY(Fact *altitude READ altitude CONSTANT)
    Q_PROPERTY(Fact *sepDist READ sepDist CONSTANT)
    Q_PROPERTY(Fact *emAltOffset READ emAltOffset CONSTANT)
    Q_PROPERTY(Fact *detectorXrayWindow READ detectorXrayWindow CONSTANT)
    Q_PROPERTY(Fact *fileName READ fileName CONSTANT)
    Q_PROPERTY(Fact *numImages READ numImages CONSTANT)
    Q_PROPERTY(Fact *overlap READ overlap CONSTANT)
    Q_PROPERTY(bool swapUavs READ swapUavs WRITE setSwapUavs NOTIFY swapUavsChanged)

    QGCMapPolyline *corridorPolyline(void) { return &_corridorPolyline; }
    QGCMapPolygon *offsetPolygon() { return &_offsetPolygon; }
    Fact *altitude() { return &_altitudeFact; }
    Fact *sepDist() { return _sepDistFact; }
    Fact *emAltOffset() { return _emAltOffsetFact; }
    Fact *detectorXrayWindow() { return _detectorXrayWindowFact; }
    Fact *fileName() { return _fileNameFact; }
    Fact *numImages() { return _numImagesFact; }
    Fact *overlap() { return _overlapFact; }

    // These are called by the editor QML's polygon-capture callbacks.
    Q_INVOKABLE void clearPolyline() { _corridorPolyline.clear(); }
    Q_INVOKABLE void addPolylineCoordinate(const QGeoCoordinate &coordinate) { _corridorPolyline.appendVertex(coordinate); }
    Q_INVOKABLE void adjustPolylineCoordinate(int vertexIndex,
                                             const QGeoCoordinate &coordinate)  { _corridorPolyline.adjustVertex(vertexIndex, coordinate); }
    Q_INVOKABLE void sendLinearScanGoal();
    Q_INVOKABLE void setSwapUavs(const bool swap);

    static constexpr const char *canonicalName = "Perimeter Scan";
    static constexpr const char *jsonComplexItemTypeValue = "perimeterScan";
    static constexpr const char *settingsGroup = "PerimeterScan";

    // ComplexMissionItem overrides
    QString             patternName         () const final { return tr(canonicalName); }
    double              complexDistance     () const final { return _scanDistance; }
    double              minAMSLAltitude     () const final { return amslEntryAlt(); }
    double              maxAMSLAltitude     () const final { return amslExitAlt(); }
    int                 lastSequenceNumber  () const final;
    bool                load                (const QJsonObject &complexObject, int sequenceNumber, QString &errorString) final;
    double              greatestDistanceTo  (const QGeoCoordinate &other) const final;
    QString             mapVisualQML        () const final { return QStringLiteral("qrc:/qml/Custom/Plan/PerimeterScanMapVisual.qml"); }

    bool swapUavs() const { return _swap_uavs; }
    QGeoCoordinate lat_lon_midpoint(const QGeoCoordinate &a, const QGeoCoordinate &b);
    double angleWrap360(double angle);

    // VisualMissionItem overrides
    bool                dirty                     () const final { return _dirty; }
    bool                isSimpleItem              () const final { return false; }
    bool                isStandaloneCoordinate    () const final { return false; }
    bool                specifiesCoordinate       () const final { return _corridorPolyline.isValid(); }
    bool                specifiesAltitudeOnly     () const final { return false; }
    QString             commandDescription        () const final { return tr("Perimeter Scan"); }
    QString             commandName               () const final { return tr("Perimeter Scan"); }
    QString             abbreviation              () const final { return "P"; }
    QGeoCoordinate      coordinate                () const final;
    QGeoCoordinate      entryCoordinate           () const final { return coordinate(); }
    QGeoCoordinate      exitCoordinate            () const final;
    bool                exitCoordinateSameAsEntry () const final { return false; }
    double              editableAlt               () const final { return _altitudeFact.rawValue().toDouble(); }
    double              amslEntryAlt              () const final;
    double              amslExitAlt               () const final { return amslEntryAlt(); }
    int                 sequenceNumber            () const final { return _sequenceNumber; }
    double              specifiedFlightSpeed      () final { return std::numeric_limits<double>::quiet_NaN(); }
    double              specifiedGimbalYaw        () final { return std::numeric_limits<double>::quiet_NaN(); }
    double              specifiedGimbalPitch      () final { return std::numeric_limits<double>::quiet_NaN(); }
    void                appendMissionItems        (QList<MissionItem *> &items, QObject *missionItemParent) final;
    void                setMissionFlightStatus    (MissionFlightStatus_t &missionFlightStatus) final;
    void                applyNewAltitude          (double newAltitude) final;
    double              additionalTimeDelay       () const final { return 0; }
    ReadyForSaveState   readyForSaveState         () const final;
    void                setDirty                  (bool dirty) final;
    void                setCoordinate             (const QGeoCoordinate &coord) final;
    void                setSequenceNumber         (int sequenceNumber) final;
    void                save                      (QJsonArray &missionItems) final;

signals:
    void  swapUavsChanged();

private slots:
    void _setDirty();
    void _polylineChanged();
    void _rebuildOffsetPolygon();
    

private:
    void _recalcScanDistance();

    int _sequenceNumber = 0;
    double _scanDistance   = 0.0;
    bool _swap_uavs = false;
    QGCMapPolygon _offsetPolygon; // The polygon that shows where the drones will fly during the scan
    QGCMapPolyline _corridorPolyline;
    SettingsFact _altitudeFact;
    // These mirror Facts owned by CustomSettings (see constructor); they are
    // borrowed pointers, not locally-owned SettingsFacts, since PerimeterScan
    // has no metadata entries of its own for them.
    Fact *_sepDistFact = nullptr;
    Fact *_emAltOffsetFact = nullptr;
    Fact *_detectorXrayWindowFact = nullptr;
    Fact *_fileNameFact = nullptr;
    Fact *_numImagesFact = nullptr;
    Fact *_overlapFact = nullptr;
    Fact *_bearingFact = nullptr;
    Fact *_swapUavsFact = nullptr;
};
