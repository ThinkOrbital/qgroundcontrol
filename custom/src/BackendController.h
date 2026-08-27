#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <map>
#include <chrono>
#include <QGeoCoordinate>
#include <QQuaternion>
#include <QDateTime>
#include <QMap>

#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "mavlink.h"
#include "MAVLinkProtocol.h"

enum class FlightState{no_conn, init, takeoff, coord_flight, alignment, descend, scan, operator_input, rtl, linear_scan, test};

enum class DetStatus : uint8_t {
     no_connection = 0,
     connected = 1,
     connection_failed = 2,
     capture_failed = 3,
     xray_window_failed = 4,
     offset_cal_failed = 5
};

enum class StartMission : uint8_t {
    mission_idle = 0,
    mission_start = 1,
    mission_resume = 2,
    mission_end = 3,
    mission_targ_update = 4,
    mission_scan = 5
};

enum class StartScan : uint8_t {
    scan_off = 0,
    scan_on = 1,
    scan_hard_kill = 2,
    scan_cal = 3,
    scan_tube_season = 4
};

enum class AckType : uint8_t {
        ack_none = 0,
        ack_target = 1,
        ack_coop_init = 2,
        ack_coop_takeoff = 3,
        ack_coop_flight = 4,
        ack_coop_align = 5,
        ack_coop_descend = 6,
        ack_coop_scan = 7,
        ack_coop_linear_scan = 8,
        ack_coop_rtl = 9,
        ack_coop_opin = 10,
        ack_start_start = 11,
        ack_start_resume = 12,
        ack_start_end = 13,
        ack_scan = 14
};

struct TelemetryStruct {
    bool HVvoltage_read = false;
    uint32_t HVvoltage_V = 0;

    bool HVcurrent_read = false;
    uint32_t HVcurrent_uA = 0;

    bool temperature_read = false;
    int32_t temperature_Cp1 = 0;

    bool FILcurrent_read = false;
    uint32_t FILcurrent_mA = 0;

    bool BATvoltage_read = false;
    uint32_t BATvoltage_mV = 0;

    bool faultListRead = false;
    std::string faultList = "";

    bool statusXrayOnRead = false;
    bool statusXrayOn = false;
};

class BackendController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanMissionMode READ scanMissionMode WRITE setScanMissionMode NOTIFY scanMissionModeChanged)
    Q_PROPERTY(QGeoCoordinate centerCoordinate READ centerCoordinate WRITE setCenterCoordinate NOTIFY centerCoordinateChanged)
    Q_PROPERTY(QGeoCoordinate startCoordinate READ startCoordinate WRITE setStartCoordinate NOTIFY startCoordinateChanged)
    Q_PROPERTY(QGeoCoordinate endCoordinate READ endCoordinate WRITE setEndCoordinate NOTIFY endCoordinateChanged)
    Q_PROPERTY(bool linearScan READ linearScan WRITE setLinearScan NOTIFY linearScanChanged)
    Q_PROPERTY(uint8_t overlap READ overlap WRITE setOverlap NOTIFY overlapChanged)
    Q_PROPERTY(double sepDistance READ sepDistance WRITE setSepDistance NOTIFY sepDistanceChanged)
    Q_PROPERTY(double bearing READ bearing WRITE setBearing NOTIFY bearingChanged)
    Q_PROPERTY(double targetAlt READ targetAlt WRITE setTargetAlt NOTIFY targetAltChanged)
    Q_PROPERTY(double detOffset READ detOffset WRITE setDetOffset NOTIFY detOffsetChanged)
    Q_PROPERTY(double emAltOffset READ emAltOffset WRITE setEmAltOffset NOTIFY emAltOffsetChanged)
    Q_PROPERTY(double flightAlt READ flightAlt WRITE setFlightAlt NOTIFY flightAltChanged)
    Q_PROPERTY(double flightVel READ flightVel WRITE setFlightVel NOTIFY flightVelChanged)
    Q_PROPERTY(QGeoCoordinate emitterGoalCoord READ emitterGoalCoord NOTIFY emitterGoalCoordChanged)
    Q_PROPERTY(QGeoCoordinate detectorGoalCoord READ detectorGoalCoord NOTIFY detectorGoalCoordChanged)
    Q_PROPERTY(QGeoCoordinate emitterCurrentPos READ emitterCurrentPos NOTIFY emitterCurrentPosChanged)
    Q_PROPERTY(QGeoCoordinate detectorCurrentPos READ detectorCurrentPos NOTIFY detectorCurrentPosChanged)
    Q_PROPERTY(QGeoCoordinate emitterHomePos READ emitterHomePos NOTIFY emitterHomePosChanged)
    Q_PROPERTY(QGeoCoordinate detectorHomePos READ detectorHomePos NOTIFY detectorHomePosChanged)
    Q_PROPERTY(QString emitterGpsFix READ emitterGpsFix NOTIFY emitterGpsFixChanged)
    Q_PROPERTY(QString detectorGpsFix READ detectorGpsFix NOTIFY detectorGpsFixChanged)
    Q_PROPERTY(int32_t emitterSatCount READ emitterSatCount NOTIFY emitterSatCountChanged)
    Q_PROPERTY(int32_t detectorSatCount READ detectorSatCount NOTIFY detectorSatCountChanged)
    Q_PROPERTY(float emitterHeightAgl READ emitterHeightAgl NOTIFY emitterHeightAglChanged)
    Q_PROPERTY(float detectorHeightAgl READ detectorHeightAgl NOTIFY detectorHeightAglChanged)

    //button enable/disable
    Q_PROPERTY(bool isStartMissionButtonEn READ isStartMissionButtonEn WRITE setStartMissionButtonEn NOTIFY startMissionButtonChanged);
    Q_PROPERTY(bool isResumeMissionButtonEn READ isResumeMissionButtonEn WRITE setResumeMissionButtonEn NOTIFY resumeMissionButtonChanged);
    Q_PROPERTY(bool isStartScanButtonEn READ isStartScanButtonEn WRITE setStartScanButtonEn NOTIFY startScanButtonChanged);
    Q_PROPERTY(bool isStopScanButtonEn READ isStopScanButtonEn WRITE setStopScanButtonEn NOTIFY stopScanButtonChanged);
    Q_PROPERTY(bool isSendGoalButtonEn READ isSendGoalButtonEn WRITE setSendGoalButtonEn NOTIFY sendGoalButtonChanged);
    Q_PROPERTY(bool isEndMissionButtonEn READ isEndMissionButtonEn WRITE setEndMissionButtonEn NOTIFY endMissionButtonChanged);

    Q_PROPERTY(uint16_t detectorXrayWindow READ detectorXrayWindow WRITE setXrayWindow NOTIFY xrayWindowChanged)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(uint8_t numImages READ numImages WRITE setNumImages NOTIFY numImagesChanged)

    //payload status
    Q_PROPERTY(QString emModel READ emModel NOTIFY emModelChanged)
    Q_PROPERTY(QString emSerial READ emSerial NOTIFY emSerialChanged)
    Q_PROPERTY(uint32_t HVVoltage READ HVVoltage NOTIFY HVVoltageChanged)
    Q_PROPERTY(float HVCurrent READ HVCurrent NOTIFY HVCurrentChanged)
    Q_PROPERTY(float EmTemp READ EmTemp NOTIFY EmTempChanged)
    Q_PROPERTY(uint32_t FILCurrent READ FILCurrent NOTIFY FILCurrentChanged)
    Q_PROPERTY(float BATVoltage READ BATVoltage NOTIFY BATVoltageChanged)
    Q_PROPERTY(bool emitterConnected READ emitterConnected WRITE setEmitterConnected NOTIFY emitterConnectionChanged)
    Q_PROPERTY(bool detectorConnected READ detectorConnected WRITE setDetectorConnected NOTIFY detectorConnectionChanged)

    Q_PROPERTY(QString detVer READ detVer NOTIFY detVerChanged)
    Q_PROPERTY(uint64_t detInt READ detInt NOTIFY detIntChanged)
    Q_PROPERTY(float detBatV READ detBatV NOTIFY detBatVChanged)
    Q_PROPERTY(uint8_t detBatPerc READ detBatPerc NOTIFY detBatPercChanged)
    Q_PROPERTY(uint8_t detBatExtPow READ detBatExtPow NOTIFY detBatExtPowChanged)
    Q_PROPERTY(QString detStatus READ detStatus NOTIFY detStatusChanged)

    Q_PROPERTY(QString flightStatus READ flightStatus WRITE setFlightStatus NOTIFY flightStatusChanged)

    Q_PROPERTY(uint8_t nudgeMode READ nudgeMode WRITE setNudgeMode NOTIFY nudgeModeChanged)

public:
    explicit BackendController(QObject *parent = nullptr);
    bool scanMissionMode() const { return scanMissionMode_; }
    QGeoCoordinate centerCoordinate() const { return center_coordinate_; }
    QGeoCoordinate startCoordinate() const { return start_coordinate_; }
    QGeoCoordinate endCoordinate() const { return end_coordinate_; }
    uint8_t overlap() const { return overlap_; }
    double sepDistance() const { return sep_distance_; }
    double bearing() const { return bearing_; }
    bool swapUavs() const { return swap_uavs_; }
    bool linearScan() const {return linear_scan_;}
    double targetAlt() const { return target_alt_; }
    double detOffset() const { return detOffset_; }
    double emAltOffset() const { return emAltOffset_; }
    double flightAlt() const { return flight_alt_; }
    double flightVel() const { return flight_vel_; }
    QGeoCoordinate emitterGoalCoord() const {
        return center_coordinate_.atDistanceAndAzimuth((sep_distance_ / 2.0) - detOffset_, bearing_ + 180.0);
    }
    QGeoCoordinate detectorGoalCoord() const {
        return center_coordinate_.atDistanceAndAzimuth((sep_distance_ / 2.0) + detOffset_, bearing_);
    }
    QGeoCoordinate emitterCurrentPos() const {
        if (positions_.contains(SYSID_EMITTER)) {
            return positions_.at(SYSID_EMITTER);
        } else {
            return QGeoCoordinate();
        }
    }
    QGeoCoordinate detectorCurrentPos() const {
        if (positions_.contains(SYSID_DETECTOR)) {
            return positions_.at(SYSID_DETECTOR);
        } else {
            return QGeoCoordinate();
        }
    }
    QGeoCoordinate emitterHomePos() const {
        if (homes_.contains(SYSID_EMITTER)) {
            return homes_.at(SYSID_EMITTER);
        } else {
            return QGeoCoordinate();
        }
    }
    QGeoCoordinate detectorHomePos() const {
        if (homes_.contains(SYSID_DETECTOR)) {
            return homes_.at(SYSID_DETECTOR);
        } else {
            return QGeoCoordinate();
        }
    }

    QString emitterGpsFix() const {
        if(qstr_gps_fix_map_.contains(SYSID_EMITTER)) {
            return qstr_gps_fix_map_.at(SYSID_EMITTER);
        } else {
            return QString::fromStdString("");
        }
    }

    QString detectorGpsFix() const {
        if(qstr_gps_fix_map_.contains(SYSID_DETECTOR)) {
            return qstr_gps_fix_map_.at(SYSID_DETECTOR);
        } else {
            return QString::fromStdString("");
        }
    }

    int32_t detectorSatCount() const {
        if(gps_sats_map_.contains(SYSID_DETECTOR)){
            return gps_sats_map_.at(SYSID_DETECTOR);
        } else {
            return 0;
        }
    }

    int32_t emitterSatCount() const {
        if(gps_sats_map_.contains(SYSID_EMITTER)){
            return gps_sats_map_.at(SYSID_EMITTER);
        } else {
            return 0;
        }
    }

    float detectorHeightAgl() const {
        if(altitudes_.contains(SYSID_DETECTOR))
        {
            return altitudes_.at(SYSID_DETECTOR);
        } else {
            return 0;
        }
    }

    float emitterHeightAgl() const {
        if(altitudes_.contains(SYSID_EMITTER))
        {
            return altitudes_.at(SYSID_EMITTER);
        } else {
            return 0;
        }
    }

    bool isStartMissionButtonEn() const { return this->isStartMissionButtonEn_; }
    bool isResumeMissionButtonEn() const { return this->isResumeMissionButtonEn_; }
    bool isStartScanButtonEn() const { return this->isStartScanButtonEn_; }
    bool isStopScanButtonEn() const { return this->isStopScanButtonEn_; }
    bool isSendGoalButtonEn() const { return this->isSendGoalButtonEn_; }
    bool isEndMissionButtonEn() const { return this->isEndMissionButtonEn_; }
    

    //payload settings
    uint32_t emitterTelemetryCadenceMs() const { return this->em_telem_cadence_ms_; }
    uint32_t emitterTestDurationMs() const { return this->em_test_duration_ms_; }
    uint32_t emitterXrayExposureMs() const { return this->em_xray_exposure_ms_; }
    uint32_t emitterXrayVoltageKv() const { return this->em_xray_voltage_kV_; }
    uint32_t emitterXrayCurrentUa() const { return this->em_xray_current_uA_;}
    uint32_t emitterCommsBlockTimeoutMs() const { return this->em_xray_comms_block_timout_ms_;}
    bool emitterConnected() const {return this->emConn_; };
    bool detectorConnected() const {return this->detConn_; };
    QString detStatus() const {return this->det_status_;}

    uint16_t detectorXrayWindow() const { return this->det_xray_window_ms_; }
    QString fileName() const {return this->file_name_; }
    uint8_t numImages() const {return this->num_images_; }

    //payload status
    QString emModel() const {return this->em_model_; };
    QString emSerial() const {return this->em_serial_; };
    uint32_t HVVoltage() const { return this->em_telemetry_.HVvoltage_V; };
    float HVCurrent() const { return static_cast<float>(this->em_telemetry_.HVcurrent_uA)/1000; };
    float    EmTemp() const { return static_cast<float>(this->em_telemetry_.temperature_Cp1)/10; };
    uint32_t FILCurrent() const { return this->em_telemetry_.FILcurrent_mA; };
    float BATVoltage() const { return static_cast<float>(this->em_telemetry_.BATvoltage_mV)/1000; };

    QString detVer() const { return this->det_versions_; };
    uint64_t detInt() const { return this->det_integration_time_; }
    float detBatV() const {return this->det_battery_voltage_; }
    uint8_t detBatExtPow() const {return this->det_battery_ext_pow_; }
    uint8_t detBatPerc() const {return this->det_battery_charge_; }

    QString flightStatus() const {return this->flight_status_; }

    uint8_t nudgeMode() const {return this->nudge_mode_; }
    
    //enable/disable buttons
    Q_INVOKABLE void setStartMissionButtonEn(const bool enabled);
    Q_INVOKABLE void setResumeMissionButtonEn(const bool enabled);
    Q_INVOKABLE void setStartScanButtonEn(const bool enabled);
    Q_INVOKABLE void setStopScanButtonEn(const bool enabled);
    Q_INVOKABLE void setSendGoalButtonEn(const bool enabled);
    Q_INVOKABLE void setEndMissionButtonEn(const bool enabled);

    //detect GUI changes and button presses
    Q_INVOKABLE void nudge(const double azimuth, const double distance);
    Q_INVOKABLE void setScanMissionMode(const bool mode);
    Q_INVOKABLE void setCenterCoordinate(const QGeoCoordinate &coord);
    Q_INVOKABLE void setStartCoordinate(const QGeoCoordinate &coord);
    Q_INVOKABLE void setEndCoordinate(const QGeoCoordinate &coord);
    Q_INVOKABLE void setOverlap(const uint8_t overlap);
    Q_INVOKABLE void setSepDistance(const double distance);
    Q_INVOKABLE void setBearing(const double bearing);
    Q_INVOKABLE void setTargetAlt(const double altitude);
    Q_INVOKABLE void setDetOffset(const double offset);
    Q_INVOKABLE void setEmAltOffset(const double offset);
    Q_INVOKABLE void setFlightAlt(const double flightAlt);
    Q_INVOKABLE void setFlightVel(const double flightVel);
    Q_INVOKABLE void sendCenterGoal();
    Q_INVOKABLE void sendLinearScanGoal();
    Q_INVOKABLE void startMission();
    Q_INVOKABLE void resumeMission();
    Q_INVOKABLE void endMission();
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void killScan();
    Q_INVOKABLE void emTubeSeasoning();
    Q_INVOKABLE void payloadCal();
    Q_INVOKABLE void setSwapUavs(const bool swap);
    Q_INVOKABLE void setLinearScan(const bool linear_scan);

    //payload settings
    Q_INVOKABLE void setCadence(const uint32_t telemCadence);
    Q_INVOKABLE void setTestDuration(const uint32_t testDuration);
    Q_INVOKABLE void setExposure(const uint32_t xrayExposure);
    Q_INVOKABLE void setVoltage(const uint32_t xrayVoltage);
    Q_INVOKABLE void setCurrent(const uint32_t xrayCurrent);
    Q_INVOKABLE void setCommTimeout(const uint32_t commTimout);
    Q_INVOKABLE void setXrayWindow(const uint16_t xrayWindow);
    Q_INVOKABLE void setFileName(const QString fileName);
    Q_INVOKABLE void setNumImages(const uint8_t numImages);

    Q_INVOKABLE void setEmitterConnected(const bool emitterConn);
    Q_INVOKABLE void setDetectorConnected(const bool detectorConn);

    Q_INVOKABLE void setFlightStatus(const QString flightstatus);

    Q_INVOKABLE void setNudgeMode(const uint8_t nudgeMode);
    Q_INVOKABLE void toggleNudgeMode();

signals:
    void scanMissionModeChanged();
    void homePositionUpdated(const QGeoCoordinate &coord);
    void connectionResult(bool success);
    void onConnectionStateChange(bool connected, uint8_t sysid);
    void centerCoordinateChanged();
    void startCoordinateChanged();
    void linearScanChanged();
    void overlapChanged();
    void endCoordinateChanged();
    void sepDistanceChanged();
    void bearingChanged();
    void targetAltChanged();
    void detOffsetChanged();
    void emAltOffsetChanged();
    void flightAltChanged();
    void flightVelChanged();
    void emitterGoalCoordChanged();
    void detectorGoalCoordChanged();
    void emitterCurrentPosChanged();
    void detectorCurrentPosChanged();
    void detectorHomePosChanged();
    void emitterHomePosChanged();
    void detectorGpsFixChanged();
    void emitterGpsFixChanged();
    void detectorSatCountChanged();
    void emitterSatCountChanged();
    void detectorHeightAglChanged();
    void emitterHeightAglChanged();

    void startMissionButtonChanged();
    void resumeMissionButtonChanged();
    void startScanButtonChanged();
    void stopScanButtonChanged();
    void sendGoalButtonChanged();
    void endMissionButtonChanged();

    //payload settings
    void cadenceChanged();
    void testDurationChanged();
    void exposureChanged();
    void voltageChanged();
    void currentChanged();
    void commTimeoutChanged();
    void xrayWindowChanged();
    void fileNameChanged();
    void numImagesChanged();

    //payload status
    void emModelChanged();
    void emSerialChanged();
    void HVVoltageChanged();
    void HVCurrentChanged();
    void EmTempChanged();
    void FILCurrentChanged();
    void BATVoltageChanged();
    void emitterConnectionChanged();
    void detectorConnectionChanged();

    void detVerChanged();
    void detIntChanged();
    void detBatVChanged();
    void detBatExtPowChanged();
    void detBatPercChanged();
    void detStatusChanged();

    void flightStatusChanged();
    void nudgeModeChanged(uint8_t nudgeMode);

private slots:
    void _mavlinkMessageReceived(LinkInterface* link, mavlink_message_t message);
    void _vehicleAdded(Vehicle* vehicle);
    void _vehicleRemoved(Vehicle* vehicle);

private:

    void sendStartMission(StartMission state);
    void sendStartScan(StartScan state);
    void send_ack(AckType type, uint8_t src_id);
    void sendGoal(mavlink_cooperative_target_definition_t& msg);

    QGeoCoordinate center_coordinate_ {40.0156293, -105.2207272};
    QGeoCoordinate start_coordinate_;
    QGeoCoordinate end_coordinate_;
    uint8_t overlap_ {0};
    double sep_distance_ { 10.0 };
    double bearing_ { 0.0 };
    double target_alt_ { 2.0 };
    double detOffset_ { 0.0 };
    double emAltOffset_ {0.5};
    double flight_alt_ {10.0};
    double flight_vel_ {3.0};

    bool swap_uavs_ = false;
    bool linear_scan_ = false;

    bool scanMissionMode_ = {true};
    bool isStartMissionButtonEn_ = {false};
    bool isResumeMissionButtonEn_ = {false};
    bool isStartScanButtonEn_ = {false};
    bool isStopScanButtonEn_ = {false};
    bool isSendGoalButtonEn_ = {false};
    bool isEndMissionButtonEn_ = {false};
    
    bool targMsgSent_ = {false};
    bool calMsgSent_ = {false};
    bool emConn_ = {false};
    bool detConn_ = {false};
    bool descend2Targ = {false};
    bool mapCentered_ = {false};

    bool singleUAV_ = {false};

    //payload
    StartScan scan_state_ {StartScan::scan_off};

    //detector settings
    uint16_t det_xray_window_ms_ {1000};
    uint64_t det_integration_time_ {};
    float    det_battery_voltage_ {};
    uint8_t  det_battery_ext_pow_ {};
    uint8_t  det_battery_charge_ {};
    QString  det_versions_ {};
    QString  det_status_ {};
    QString  file_name_ {"image"};
    uint8_t  num_images_{1};
    uint8_t  nudge_mode_ {0};

    //emitter settings
    uint32_t em_test_duration_ms_ {5000};
    uint32_t em_telem_cadence_ms_ {500};
    uint32_t em_xray_exposure_ms_ {7000};
    uint32_t em_xray_voltage_kV_  {150};
    uint32_t em_xray_current_uA_  {1000};
    uint32_t em_xray_comms_block_timout_ms_ {1000};

    TelemetryStruct em_telemetry_;
    
    QString em_model_ {};
    QString em_serial_ {};
    QString flight_status_ {"Waiting for Connection..."};

    // Vehicle positions keyed by SYSID
    std::map<uint8_t, QGeoCoordinate> positions_;
    // Vehicle homes keyed by SYSID
    std::map<uint8_t, QGeoCoordinate> homes_;
    // Vehicle positions keyed by SYSID
    std::map<uint8_t, QGeoCoordinate> eulers_;
    // Vehicle Altitude
    std::map<uint8_t, float> altitudes_;
    // Vehicle GPS fix
    std::map<uint8_t, QString> qstr_gps_fix_map_;
    std::map<uint8_t, int32_t> gps_sats_map_;

    std::map<uint8_t, AckType> msg_ack_map_;

    std::map<uint8_t, FlightState> flight_state_map_;

    QMap<uint8_t, qint64> heartbeat_last_seen_ms_;  // sysid → last heartbeat timestamp
    static constexpr int HEARTBEAT_TIMEOUT_MS = 5000; // 5 second timeout
  
    Vehicle* _vehicle = nullptr;


    // // Mission State
    // std::map<uint8_t, uint8_t> uav_state_map_;
    // Subscription Status
    std::map<uint8_t, bool> subscribed_map_;
    std::map<uint8_t, bool> connection_status_map_; //ToDo: keep track of connected/disconnected

    bool sent_start_msg_ {false};
    bool sent_scan_msg_ {false};
    bool sent_targ_msg_ {false};

    std::chrono::steady_clock::time_point start_msg_time_;
    std::chrono::steady_clock::time_point scan_msg_time_;
    std::chrono::steady_clock::time_point targ_msg_time_;

    StartMission prev_mission_msg_state_;
    StartScan prev_scan_msg_state_;

    std::atomic<bool> position_updated_{false};
    std::atomic<bool> gps_info_updated_{false};
    std::atomic<bool> attitude_updated_{false};
    std::atomic<bool> pose_updated_{false};
    std::atomic<bool> uav_state_updated_{false};
    std::atomic<bool> det_state_updated_{false};
    std::atomic<bool> em_state_updated_{false};
    std::mutex data_mutex_;

    // void OnNewSysID(const uint8_t sysid);
    void processTelemetryUpdates();

    const uint8_t SYSID_EMITTER {2}; //id for emitter FCU
    const uint8_t SYSID_DETECTOR {1}; //id for detector FCU
    const uint8_t SYSID_EMITTER_COMP {22};
    const uint8_t SYSID_DETECTOR_COMP {11};
    const uint8_t SYSID_RTK {124};
};

//template to check multiple enum types at once.
template<typename T, typename... Args>
bool is_one_of(T val, Args... args) {
    return ((val == args) || ...);
}


