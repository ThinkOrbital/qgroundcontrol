#include "BackendController.h"
#include <QtConcurrent>
#include <QMetaObject>
#include <QGeoCoordinate>
#include <QtMath>

#include "Comms/MAVLinkProtocol.h"

// Constructor
BackendController::BackendController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<uint8_t>("uint8_t");

    // Hook into all incoming MAVLink messages (post-parse)
    MAVLinkProtocol* mavlinkProtocol = MAVLinkProtocol::instance();
    connect(mavlinkProtocol, &MAVLinkProtocol::messageReceived,
            this, &BackendController::_mavlinkMessageReceived,
            Qt::QueuedConnection); // QueuedConnection since messages arrive on non-GUI thread
   
    // Track vehicles being added/removed (handles hot-connect)
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    connect(mgr, &MultiVehicleManager::vehicleAdded,
            this, &BackendController::_vehicleAdded);
    connect(mgr, &MultiVehicleManager::vehicleRemoved,
            this, &BackendController::_vehicleRemoved);

    // Register any vehicles already connected at startup
    QmlObjectListModel* vehicles = mgr->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        Vehicle* v = qobject_cast<Vehicle*>(vehicles->get(i));
        if (v) _vehicleAdded(v);
    }

 
    // Process telemetry updates at 10 Hz (every 100ms)
    QTimer *telemetryTimer = new QTimer(this);
    connect(telemetryTimer, &QTimer::timeout, this, &BackendController::processTelemetryUpdates);
    telemetryTimer->start(200);
}


void BackendController::_vehicleAdded(Vehicle* vehicle)
{
    if (!vehicle) return;

    uint8_t sysid = vehicle->id();
    connection_status_map_[sysid] = true;

    if (sysid == SYSID_EMITTER) {
        // setEmitterConnected(true);
        // setFlightStatus("Emitter Connected");
        qDebug() << "Emitter with sysid " << sysid << " connected";

    } else if (sysid == SYSID_DETECTOR) {
        // setDetectorConnected(true);
        // setFlightStatus("Detector Connected");
        qDebug() << "Detector with sysid " << sysid << " connected";
    }
}

void BackendController::_vehicleRemoved(Vehicle* vehicle)
{
    if (!vehicle) return;

    uint8_t sysid = vehicle->id();
    connection_status_map_[sysid] = false;

    // Clean up stale data
    positions_.erase(sysid);
    altitudes_.erase(sysid);
    homes_.erase(sysid);

    if (sysid == SYSID_EMITTER) {
        // setEmitterConnected(false);
        this->targMsgSent_ = false;
        this->calMsgSent_ = false;
        setFlightStatus("Emitter Disconnected");
        qDebug() << "Emitter with sysid " << sysid << " disconnected";
    } else if (sysid == SYSID_DETECTOR) {
        this->targMsgSent_ = false;
        this->calMsgSent_ = false;
        // setDetectorConnected(false);
        setFlightStatus("Detector Disconnected");
        qDebug() << "Detector with sysid " << sysid << " disconnected";
    }
}

void BackendController::_mavlinkMessageReceived(LinkInterface* link, mavlink_message_t message)
{
    Q_UNUSED(link)
    
    // Filter to only two drones
    if (message.sysid != SYSID_EMITTER && message.sysid != SYSID_DETECTOR && message.sysid != SYSID_DETECTOR_COMP && message.sysid != SYSID_EMITTER_COMP) {
        return;
    }

    //messages coming directly from FCU
    if(message.sysid == SYSID_EMITTER || message.sysid == SYSID_DETECTOR)
    {
        switch (message.msgid) {
            case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
                mavlink_global_position_int_t pos;
                mavlink_msg_global_position_int_decode(&message, &pos);

                QGeoCoordinate coord(
                    pos.lat / 1e7,
                    pos.lon / 1e7,
                    pos.alt / 1000.0
                );
                positions_[message.sysid] = coord;
                altitudes_[message.sysid] = pos.relative_alt / 1000.0f;

                if (message.sysid == SYSID_EMITTER) {
                    emit emitterCurrentPosChanged();
                    emit emitterHeightAglChanged();
                } else {
                    emit detectorCurrentPosChanged();
                    emit detectorHeightAglChanged();
                }
                break;
            }
        }
    }
    
  
    if(message.sysid == SYSID_EMITTER_COMP || message.sysid == SYSID_DETECTOR_COMP)
    {

        this->subscribed_map_[message.sysid] = true;

        switch (message.msgid) {

            case MAVLINK_MSG_ID_HEARTBEAT: {
                this->heartbeat_last_seen_ms_[message.sysid] = QDateTime::currentMSecsSinceEpoch(); 
                break;
            }

            case MAVLINK_MSG_ID_COOPERATIVE_STATE: {
                mavlink_cooperative_state_t coop_state;
                mavlink_msg_cooperative_state_decode(&message, &coop_state);
                // store/emit based on sysid
                qDebug() << "Received coop state of " << coop_state.state << " from sysid " << message.sysid;
        
                this->uav_state_map_[message.sysid] = coop_state.state;
                this->uav_state_updated_.store(true);
                if(coop_state.state == 6) //ToDo: need to add enum for UAV states to QGC
                {
                    qDebug() << "Sending out Acknowledge message";
                    this->send_ack(AckType::ack_coop_opin);
                }

                break;
            }

            case MAVLINK_MSG_ID_EM_STATUS: {
                mavlink_em_status_t em_status;
                mavlink_msg_em_status_decode(&message, &em_status);

                qDebug() << "Received em status";
                
                
                this->em_telemetry_.HVvoltage_V = em_status.em_hv_voltage_v;
                this->em_telemetry_.HVcurrent_uA = em_status.em_hv_current_uA;
                this->em_telemetry_.FILcurrent_mA = em_status.em_fil_current_mA;
                this->em_telemetry_.BATvoltage_mV = em_status.em_bat_voltage_mV;
                this->em_telemetry_.temperature_Cp1 = em_status.em_temp_c;
                this->em_model_ = em_status.em_model;
                this->em_serial_ = em_status.em_serial;
                this->em_state_updated_.store(true);
  
                break;
            }

            case MAVLINK_MSG_ID_DET_STATUS: {
                mavlink_det_status_t det_status;
                mavlink_msg_det_status_decode(&message, &det_status);
                
                qDebug() << "Received det status";

                // this->det_status_ = static_cast<DetStatus>(det_status.det_state);
                this->det_versions_ = det_status.det_version;
                this->det_integration_time_ = det_status.det_int_time_ms;
                this->det_battery_voltage_ = det_status.det_bat_volt;
                this->det_battery_ext_pow_ = det_status.det_dc_present;
                this->det_battery_charge_ = det_status.det_bat_charge;
                DetStatus state = static_cast<DetStatus> (det_status.det_state);

                switch(state)
                {
                    case DetStatus::no_connection:
                        this->det_status_ = "Not Connected";
                        break;
                    case DetStatus::connected:
                        this->det_status_ = "Connected";
                        break;
                    case DetStatus::connection_failed:
                        this->det_status_ = "Failed to Connect";
                        break;
                    case DetStatus::capture_failed:
                        this->det_status_ = "Capture Failed";
                        break;
                    case DetStatus::xray_window_failed:
                        this->det_status_ = "Set XRay Window Failed";
                        break;
                    case DetStatus::offset_cal_failed:
                        this->det_status_ = "Offset Calibration Failed";
                        break;
                    default:
                        this->det_status_ = "Unkown Det Status";
                        break;
                }

                this->det_state_updated_.store(true);
                break;
            }
            
            case MAVLINK_MSG_ID_MSG_ACK: {
                mavlink_msg_ack_t msg_ack;
                mavlink_msg_msg_ack_decode(&message, &msg_ack);
                
                this->msg_ack_map_[message.sysid] = static_cast<AckType> (msg_ack.ack_type);

                if(this->subscribed_map_[SYSID_EMITTER_COMP] && this->subscribed_map_[SYSID_DETECTOR_COMP])
                {
                    if((this->msg_ack_map_[SYSID_EMITTER_COMP] == AckType::ack_target) && (this->msg_ack_map_[SYSID_DETECTOR_COMP] == AckType::ack_target))
                    {
                        this->targMsgSent_ = true;
                        this->uav_state_updated_.store(true);
                    }
                
                    
                    if((this->msg_ack_map_[SYSID_EMITTER_COMP] == AckType::ack_scan) && (this->msg_ack_map_[SYSID_DETECTOR_COMP] == AckType::ack_scan))
                    {
                        qDebug() << "Setting calMsgSent to true ";
                        this->calMsgSent_ = true;
                        this->uav_state_updated_.store(true);
                    }

                }
                else if(this->subscribed_map_[SYSID_EMITTER_COMP] || this->subscribed_map_[SYSID_DETECTOR_COMP])
                {
                    if((this->msg_ack_map_[SYSID_EMITTER_COMP] == AckType::ack_target) || (this->msg_ack_map_[SYSID_DETECTOR_COMP] == AckType::ack_target))
                    {
                        this->targMsgSent_ = true;
                        this->uav_state_updated_.store(true);
                    }
                
                    
                    if((this->msg_ack_map_[SYSID_EMITTER_COMP] == AckType::ack_scan) || (this->msg_ack_map_[SYSID_DETECTOR_COMP] == AckType::ack_scan))
                    {
                        this->calMsgSent_ = true;
                        this->uav_state_updated_.store(true);
                    }
                }
       

                break;
            }

            default:
                break;
        }
    }
}

void BackendController::processTelemetryUpdates()
{
    std::lock_guard<std::mutex> lock(this->data_mutex_);

    // --- Companion computer timeout check ---
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QList<uint8_t> compSysIds = { SYSID_EMITTER_COMP, SYSID_DETECTOR_COMP };

    for (uint8_t sysid : compSysIds)
    {
        if (!heartbeat_last_seen_ms_.contains(sysid)) continue; // never seen yet

        bool wasConnected = subscribed_map_[sysid];
        bool timedOut = (now - heartbeat_last_seen_ms_[sysid]) > HEARTBEAT_TIMEOUT_MS;

        if (wasConnected && timedOut)
        {
            qDebug() << "Companion computer sysid" << sysid << "timed out!";
            subscribed_map_[sysid] = false;

            if (sysid == SYSID_EMITTER_COMP) {
                this->targMsgSent_ = false;
                this->calMsgSent_ = false;
                setFlightStatus("Emitter Companion Disconnected");
                // emit whatever UI signal you need
            } else if (sysid == SYSID_DETECTOR_COMP) {
                this->targMsgSent_ = false;
                this->calMsgSent_ = false;
                setFlightStatus("Detector Companion Disconnected");
            }

            this->uav_state_updated_.store(true);
        }
    }

    //uav state changed
    if(this->uav_state_updated_.load())
    {
     
        for(const auto& [sysid, state] : this->uav_state_map_)
        {
           
            qDebug() << "UAV " << +sysid << " changed its state to " << +this->uav_state_map_[sysid];
    
            switch(this->uav_state_map_[sysid])
            {
                case 0: //init state
                {
                    qDebug() << "Inside of init state";
                    qDebug() << "subscribed_map_[" << +sysid << "] = " << subscribed_map_[sysid];
                    if(this->subscribed_map_[SYSID_EMITTER_COMP]&& this->subscribed_map_[SYSID_DETECTOR_COMP])
                    {
                        if((this->uav_state_map_[SYSID_EMITTER_COMP] == 0) && (this->uav_state_map_[SYSID_DETECTOR_COMP] == 0))
                        {
                            QString str_flight_status;
                            // if(targMsgSent_ && (this->uav_state_map_[sysid] == 0) && (this->uav_state_map_[sysid] == 0))
                            // {
                                
                                
                            // }
                            if(calMsgSent_ && (this->uav_state_map_[SYSID_EMITTER_COMP] == 0) && (this->uav_state_map_[SYSID_DETECTOR_COMP] == 0))
                            {
                                this->setStartScanButtonEn(false);
                            }
                            
                            if(targMsgSent_ && calMsgSent_)
                            {
                                str_flight_status = "Waiting for user to press start mission button.";
                                qDebug() << "Start Mission Button should be enabled";
                                this->setStartMissionButtonEn(true);
                            }

                            if(!targMsgSent_)
                            {
                                str_flight_status += "Waiting for user to send target information to UAVs. \n";
                            }

                            if(!calMsgSent_)
                            {
                                str_flight_status += "Waiting for user to calibrate detector. ";
                            }

                            if(!str_flight_status.isEmpty())
                            {
                                this->setFlightStatus(str_flight_status);
                            }

                            this->setResumeMissionButtonEn(false);
                            this->setSendGoalButtonEn(true);
                        }
                    } 
                    else if(this->subscribed_map_[SYSID_DETECTOR_COMP] || this->subscribed_map_[SYSID_EMITTER_COMP])
                    {
                        qDebug() << "Inside subscribed_map_ check";
                        QString str_flight_status;
                        // if(targMsgSent_ && (this->uav_state_map_[SYSID_DETECTOR_COMP] == 0) || (this->uav_state_map_[SYSID_EMITTER_COMP] == 0))
                        // {
                        //     this->setStartMissionButtonEn(true);
                            
                        // }
                        if(calMsgSent_ && ((this->uav_state_map_[SYSID_DETECTOR_COMP] == 0) || (this->uav_state_map_[SYSID_EMITTER_COMP] == 0)))
                        {
                            this->setStartScanButtonEn(false);
                        }
                        
                        if(targMsgSent_ && calMsgSent_)
                        {
                            this->setStartMissionButtonEn(true);
                            qDebug() << "Start Mission Button should be enabled";
                            str_flight_status = "Waiting for user to press start mission button.";
                        }

                        if(!targMsgSent_)
                        {
                            str_flight_status += "Waiting for user to send target information to UAVs. \n";
                        }

                        if(!calMsgSent_)
                        {
                            str_flight_status += "Waiting for user to calibrate detector. ";
                        }

                        if(!str_flight_status.isEmpty())
                        {
                            qDebug() << "Setting flight status to " << str_flight_status;
                            this->setFlightStatus(str_flight_status);
                        }

                        this->setResumeMissionButtonEn(false);
                        this->setSendGoalButtonEn(true);   
                    }
                    break;
                }
                case 1: //takeoff state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->setFlightStatus("UAVs taking off...");
                    break;
                case 2: //coord_flight state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setEndMissionButtonEn(true);
                    this->setStopScanButtonEn(false);
                    this->setFlightStatus("UAVs are flying to target...");
                    break;
                case 3: //alignment state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->descend2Targ = false;
                    this->setFlightStatus("UAVs are aligning...");
                    break;
                case 4: //descend state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->descend2Targ = true;
                    this->setFlightStatus("UAVs are descending to scan altitude...");
                    break;
                case 5: //scan state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStopScanButtonEn(false);
                    switch(this->scan_state_)
                    {
                        case StartScan::scan_off:
                            this->setFlightStatus("Stopping Scan");
                            break;
                        case StartScan::scan_on:
                            this->setFlightStatus("Performing X-Ray Scan...");
                            this->setStopScanButtonEn(true);
                            break;
                        case StartScan::scan_hard_kill:
                            this->setFlightStatus("Powering off emitter...");
                            break;
                        case StartScan::scan_cal:
                            this->setFlightStatus("Calibrating Detector...");
                            break;

                        case StartScan::scan_tube_season:
                            this->setFlightStatus("Performing Tube Seasoning...");
                            break;
                        default:
                            this->setFlightStatus("Unknown Scan State!");
                            break;

                    }
                    break;
                case 6: //operator input state
                    if(this->subscribed_map_[sysid] && this->subscribed_map_[sysid])
                    {
                        if((this->uav_state_map_[sysid] == 6) && (this->uav_state_map_[sysid] == 6))
                        {
                            this->setStartMissionButtonEn(false);
                            this->setResumeMissionButtonEn(true);
                            this->setSendGoalButtonEn(true);
                            this->setStopScanButtonEn(false);
                            this->setStartScanButtonEn(this->descend2Targ);
                            this->setEndMissionButtonEn(true);
                            if(this->descend2Targ)
                            {
                                this->setFlightStatus("Waiting for user adjustments and/or scan...");
                            } 
                            else
                            {
                                this->setFlightStatus("Waiting for user adjustments and/or resume mission...");
                            }
                        }
                    }
                    else if(this->subscribed_map_[sysid] || this->subscribed_map_[sysid])
                    {
                        this->setStartMissionButtonEn(false);
                        this->setResumeMissionButtonEn(true);
                        this->setSendGoalButtonEn(true);
                        this->setStopScanButtonEn(false);
                        this->setStartScanButtonEn(this->descend2Targ);
                        this->setEndMissionButtonEn(true);
                        if(this->descend2Targ)
                        {
                            this->setFlightStatus("Waiting for user adjustments and/or scan...");
                        } 
                        else
                        {
                            this->setFlightStatus("Waiting for user adjustments and/or resume mission...");
                        }   
                    }
                    break;
                case 7: //rtl state
                    this->setStartMissionButtonEn(false);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setEndMissionButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->setFlightStatus("UAVs are returning to home...");
                    break;
                case 8: //test state
                    this->setStartMissionButtonEn(true);
                    this->setResumeMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->setFlightStatus("UAVs are in test state...");
                    break;
                default:
                    // std::cerr << "Unknown Flight State" << std::endl;
                    qDebug() << "Unknown flight state";
                    this->setStartMissionButtonEn(false);
                    this->setStartScanButtonEn(false);
                    this->setSendGoalButtonEn(false);
                    this->setStartMissionButtonEn(false);
                    this->setStopScanButtonEn(false);
                    this->setEndMissionButtonEn(false);
                    break;
            }
        }
        this->uav_state_updated_.store(false);
    }
    
    //emitter state changed
    if(this->em_state_updated_.load())
    {
        emit emModelChanged();
        emit emSerialChanged();
        emit HVVoltageChanged();
        emit HVCurrentChanged();
        emit FILCurrentChanged();
        emit BATVoltageChanged();
        emit EmTempChanged();

        if(!this->em_model_.isEmpty())
        {
            this->setEmitterConnected(true);
        }
       
        this->em_state_updated_.store(false);
    }

    //detector state changed
    if(this->det_state_updated_.load())
    {
        
        emit detVerChanged();
        emit detIntChanged();
        emit detBatVChanged();
        emit detBatExtPowChanged();
        emit detBatPercChanged();
        emit detStatusChanged();
        if(!this->det_versions_.isEmpty())
        {
            this->setDetectorConnected(true);
        }
        
        this->det_state_updated_.store(false);
    }


    if((this->uav_state_map_[SYSID_DETECTOR_COMP] == 0) && (this->uav_state_map_[SYSID_EMITTER_COMP] == 0))
    {
        if((this->subscribed_map_[SYSID_DETECTOR_COMP] || this->subscribed_map_[SYSID_EMITTER_COMP]) && !this->calMsgSent_ && (!this->targMsgSent_) )
        {
            this->setFlightStatus("Waiting for user to send target information to UAVs. \n Waiting for user to calibrate detector.");
        }
    }

}

void BackendController::nudge(const double azimuth, const double distance)
{
    QGeoCoordinate newCoord = center_coordinate_.atDistanceAndAzimuth(distance, azimuth);
    center_coordinate_ = newCoord;
    emit centerCoordinateChanged();
    emit emitterGoalCoordChanged();
    emit detectorGoalCoordChanged();
}

void BackendController::setScanMissionMode(const bool mode)
{
    if (scanMissionMode_ != mode) {
        scanMissionMode_ = mode;
        emit scanMissionModeChanged();
    }
}

void BackendController::setCenterCoordinate(const QGeoCoordinate &coord)
{
    if (center_coordinate_ != coord) {
        center_coordinate_ = coord;
        emit centerCoordinateChanged();
        emit emitterGoalCoordChanged();
        emit detectorGoalCoordChanged();
    }
}

void BackendController::setSepDistance(const double dist)
{
    if (sep_distance_ != dist) {
        sep_distance_ = dist;
        emit sepDistanceChanged();
        emit emitterGoalCoordChanged();
        emit detectorGoalCoordChanged();
    }
}

void BackendController::setBearing(const double bearing)
{
    if (bearing_ != bearing) {
        bearing_ = bearing;
        emit bearingChanged();
        emit emitterGoalCoordChanged();
        emit detectorGoalCoordChanged();
    }
}

void BackendController::setAltitude(const double altitude)
{
    if (altitude_ != altitude) {
        altitude_ = altitude;
        emit altitudeChanged();
    }
}

void BackendController::setDetOffset(const double offset)
{
    if(this->detOffset_ != offset){
        this->detOffset_ = offset;
        emit detOffsetChanged();
        emit emitterGoalCoordChanged();
        emit detectorGoalCoordChanged();
    }
}

void BackendController::setEmAltOffset(const double offset)
{
    if(this->emAltOffset_ != offset)
    {
        this->emAltOffset_ = offset;
        emit emAltOffsetChanged();
    }
}

void BackendController::setFlightAlt(const double flightAlt)
{
    if(this->flight_alt_ != flightAlt)
    {
        this->flight_alt_ = flightAlt;
        emit flightAltChanged();
    }
}

void BackendController::setFlightVel(const double flightVel)
{
    if(this->flight_vel_ != flightVel)
    {
        this->flight_vel_ = flightVel;
        emit flightVelChanged();
    }
}

void BackendController::setStartMissionButtonEn(const bool enabled)
{
    if(this->isStartMissionButtonEn_ != enabled)
    {
        this->isStartMissionButtonEn_ = enabled;
        emit startMissionButtonChanged();
    }
}

void BackendController::setResumeMissionButtonEn(const bool enabled)
{
    if(this->isResumeMissionButtonEn_ != enabled)
    {
        this->isResumeMissionButtonEn_ = enabled;
        emit resumeMissionButtonChanged();
    }
}

void BackendController::setStartScanButtonEn(const bool enabled)
{
    if(this->isStartScanButtonEn_ != enabled)
    {
        this->isStartScanButtonEn_ = enabled;
        emit startScanButtonChanged();
    }
}

void BackendController::setStopScanButtonEn(const bool enabled)
{
    if(this->isStopScanButtonEn_ != enabled)
    {
        this->isStopScanButtonEn_ = enabled;
        emit stopScanButtonChanged();
    }
}

void BackendController::setSendGoalButtonEn(const bool enabled)
{
    if(this->isSendGoalButtonEn_ != enabled)
    {
        this->isSendGoalButtonEn_ = enabled;
        emit sendGoalButtonChanged();
    }
}

void BackendController::setEndMissionButtonEn(const bool enabled)
{
    if(this->isEndMissionButtonEn_ != enabled)
    {
        this->isEndMissionButtonEn_ = enabled;
        emit endMissionButtonChanged();
    }
}

void BackendController::sendGoal()
{
    // qDebug() << "Send Goal Button Pressed!";

     // fill out the message
    double lat_int, lat_frac, lon_int, lon_frac;
    lat_frac = modf(centerCoordinate().latitude(), &lat_int);
    lon_frac = modf(centerCoordinate().longitude(), &lon_int);

    mavlink_cooperative_target_definition_t msg = {};
    msg.lat_int      = static_cast<int32_t>(centerCoordinate().latitude());
    msg.lon_int      = static_cast<int32_t>(centerCoordinate().longitude());
    msg.lat_frac     = static_cast<int32_t>(lat_frac * 1e8);
    msg.lon_frac     = static_cast<int32_t>(lon_frac * 1e8);
    msg.altitude     = static_cast<float>(this->altitude_);
    msg.separation   = this->sep_distance_;
    msg.angle        = static_cast<uint32_t>(bearing_);
    msg.detOffset    = this->detOffset_;
    msg.emAltOffset  = this->emAltOffset_;
    msg.flightAlt    = this->flight_alt_;
    msg.flightVel    = this->flight_vel_;

   // Send to ALL connected vehicles
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    for (int i = 0; i < mgr->vehicles()->count(); i++) {
        
        Vehicle* vehicle = mgr->vehicles()->value<Vehicle*>(i);
        if (!vehicle) continue;

        SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (!sharedLink) continue;

        mavlink_message_t mavMsg;
        mavlink_msg_cooperative_target_definition_encode_chan(
            static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId()),
            static_cast<uint8_t>(MAVLinkProtocol::getComponentId()),
            sharedLink->mavlinkChannel(),
            &mavMsg,
            &msg
        );
        qDebug() << "Sending target message to system id " << vehicle->id();

        (void) vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), mavMsg);
        break; //send only one message out as it's going to both mavlink-routers on port 50882
    }

    // this->targMsgSent_ = true;
    // this->uav_state_updated_.store(true);
   
}

void BackendController::sendStartMission(uint8_t & state)
{
    mavlink_start_mission_t msg = {};
    msg.start_mission = state;

    // Send to ALL connected vehicles
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    for (int i = 0; i < mgr->vehicles()->count(); i++) {
        Vehicle* vehicle = mgr->vehicles()->value<Vehicle*>(i);
        if (!vehicle) continue;

        SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (!sharedLink){
            continue;
        } 

        mavlink_message_t mavMsg;
        mavlink_msg_start_mission_encode_chan(
            static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId()),
            static_cast<uint8_t>(MAVLinkProtocol::getComponentId()),
            sharedLink->mavlinkChannel(),
            &mavMsg,
            &msg
        );
        qDebug() << "Sending start mission message to system id " << vehicle->id() << " with state " << +state;
        (void) vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), mavMsg);
        break; //send only one message out as it's going to both mavlink-routers on port 50882
    }
}

void BackendController::sendStartScan(uint8_t & state)
{
    mavlink_start_scan_t msg = {};
    msg.start_scan = state;
    msg.em_test_duration_ms = this->emitterTestDurationMs();
    msg.em_telemetry_cadence_ms = this->emitterTelemetryCadenceMs();
    msg.em_xray_exposure_ms = this->emitterXrayExposureMs();
    msg.em_xray_voltage_kV = this->emitterXrayVoltageKv();
    msg.em_xray_current_uA = this->emitterXrayCurrentUa();
    msg.em_xray_comms_timeout_ms = this->emitterCommsBlockTimeoutMs();
    msg.det_xray_window_ms = this->detectorXrayWindow();
    msg.num_image = this->numImages();

    strncpy(msg.file_name, this->fileName().toStdString().c_str(), sizeof(msg.file_name)-1);
    msg.file_name[sizeof(msg.file_name) - 1] = '\0';

    // Send to ALL connected vehicles
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    for (int i = 0; i < mgr->vehicles()->count(); i++) {
        Vehicle* vehicle = mgr->vehicles()->value<Vehicle*>(i);
        if (!vehicle) continue;

        SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (!sharedLink){
            continue;
        }

        mavlink_message_t mavMsg;
        mavlink_msg_start_scan_encode_chan(
            static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId()),
            static_cast<uint8_t>(MAVLinkProtocol::getComponentId()),
            sharedLink->mavlinkChannel(),
            &mavMsg,
            &msg
        );

        qDebug() << "Sending start scan message to system id " << vehicle->id() << " with state " << +state;

        (void) vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), mavMsg);
        break; //send only one message out as it's going to both mavlink-routers on port 50882
    }
}

/********************* Orthomosiac**************************** */

//load GeoTiff
void BackendController::loadGeoTiff(const QString& path)
{   
    if(this->ortho_processing_) return;

    this->ortho_cancelled_ = false;
    this->ortho_ready_ = false;
    this->ortho_processing_ = true;
    this->ortho_progress_ = 0.0;
    emit orthoProcessingChanged();
    emit orthoReadyChanged();

    QString cleaned = path;
    if (cleaned.startsWith("file://"))
        cleaned = QUrl(path).toLocalFile();

    this->future_ = QtConcurrent::run([this, cleaned]()
    {
        if (!this->stepValidateAndOpen(cleaned)) return;

        this->ortho_file_name_ = QFileInfo(cleaned).fileName();
        emit orthoFileNameChanged();

        if (this->ortho_cancelled_) return;
        if (!this->stepReprojectToWebMercator()) return;

        if (this->ortho_cancelled_) return;
        if (!this->stepBuildOverviews()) return;

        if (this->ortho_cancelled_) return;
        if (!this->stepStartTileServer()) return;

        this->ortho_ready_ = true;
        this->ortho_processing_ = false;

        emit orthoReadyChanged();
        emit orthoProcessingChanged();
    });
}

// Step 1: Validate and Open GeoTiff
bool BackendController::stepValidateAndOpen(const QString& path)
{
    this->setOrthoProgress(0.0, "Opening GeoTiff...");

    // Clean up any previous session — marshal to main thread
    QMetaObject::invokeMethod(this, [this]() {
        this->cleanupPreviousSession();
    }, Qt::BlockingQueuedConnection);

    GDALAllRegister();
    QByteArray bytes = path.toUtf8();
    
    this->gdal_dataset_ = (GDALDataset*)GDALOpen(bytes.constData(), GA_ReadOnly);
    if(!this->gdal_dataset_)
    {
        emit errorOccurred("Failed to open GeoTiff: " + path);
        return false;
    }

    // Verify it has a valid projection
    if(QString(this->gdal_dataset_->GetProjectionRef()).isEmpty())
    {
        emit errorOccurred("GeoTiff has no projection information.");
        GDALClose(this->gdal_dataset_);
        this->gdal_dataset_ = nullptr;
        return false;
    }

    this->setOrthoProgress(5.0, "Validated GeoTiff");
    return true;
}   

// Step 2: Reproject to Web Mercator (EPSG:3857)
bool BackendController::stepReprojectToWebMercator()
{
    this->setOrthoProgress(5.0, "Creating Web Mercator VRT...");

    if (!this->gdal_dataset_)
    {
        emit errorOccurred("No dataset loaded.");
        return false;
    }

    const char* targetSRS = "EPSG:3857";

    GDALDatasetH warpedVRT = GDALAutoCreateWarpedVRT(
        (GDALDatasetH)this->gdal_dataset_,
        nullptr,
        targetSRS,
        GRA_Bilinear,
        0.0,
        nullptr
    );

    if (!warpedVRT)
    {
        emit errorOccurred("Failed to create warped VRT.");
        return false;
    }

    GDALClose((GDALDatasetH)this->gdal_dataset_);
    this->gdal_dataset_ = (GDALDataset*)warpedVRT;

    this->setOrthoProgress(40.0, "Warped VRT ready");
    return true;
}

// Step 3: Build overviews (zoom levels)
bool BackendController::stepBuildOverviews()
{
    this->setOrthoProgress(40.0, "Building overviews...");

    if (!this->gdal_dataset_)
        return false;

    GDALDriverH driver = GDALGetDatasetDriver(this->gdal_dataset_);
    QString driverName = GDALGetDriverShortName(driver);

    // VRT already handles resampling well → skip if needed
    if (driverName == "VRT")
    {
        this->setOrthoProgress(70.0, "Skipping overviews (VRT)");
        return true;
    }

    int levels[] = {2, 4, 8, 16, 32};
    int count = sizeof(levels) / sizeof(int);

    CPLErr err = this->gdal_dataset_->BuildOverviews(
        "LANCZOS",
        count,
        levels,
        0,
        nullptr,
        nullptr,
        nullptr
    );

    if (err != CE_None)
    {
        emit errorOccurred("Failed to build overviews.");
        return false;
    }

    this->setOrthoProgress(70.0, "Overviews ready");
    return true;
}

// Step 5: Start tile server
bool BackendController::stepStartTileServer()
{
    bool success = false;

    // QTcpServer must be created and started on the main thread
    QMetaObject::invokeMethod(this, [this, &success]() {

        this->qtcp_server_ = new QTcpServer(this);

        connect(this->qtcp_server_, &QTcpServer::newConnection,
                this, [this]() {
            while (this->qtcp_server_->hasPendingConnections()) {
                QTcpSocket* socket = this->qtcp_server_->nextPendingConnection();

                qDebug() << "Tile client connected from"
                         << socket->peerAddress().toString();

                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    // Accumulate data into a buffer property until full
                    // HTTP request headers have arrived
                    socket->setProperty("buffer",
                        socket->property("buffer").toByteArray() + socket->readAll());

                    QByteArray buffer = socket->property("buffer").toByteArray();

                    // HTTP headers are always terminated by \r\n\r\n
                    // Wait for more data if we haven't received it yet
                    if (!buffer.contains("\r\n\r\n"))
                        return;

                    this->handleTileRequest(socket, buffer);
                });

                connect(socket, &QTcpSocket::disconnected,
                        socket, &QTcpSocket::deleteLater);
            }
        });

        if (!this->qtcp_server_->listen(QHostAddress::LocalHost, 0)) {
            qWarning() << "Tile server failed to start:"
                       << this->qtcp_server_->errorString();
            success = false;
            return;
        }

        this->ortho_tile_url_ =
            QString("http://localhost:%1/tiles/{z}/{x}/{y}.png")
                .arg(this->qtcp_server_->serverPort());

        qDebug() << "Tile server running at:" << this->ortho_tile_url_;
        success = true;

    }, Qt::BlockingQueuedConnection);

    if (!success) return false;

    this->setOrthoProgress(100.0, "Ready ✓");
    return true;
}

void BackendController::handleTileRequest(QTcpSocket* socket,
                                           const QByteArray& request)
{
    // Parse: "GET /tiles/15/12345/67890.png HTTP/1.1"
    QRegularExpression re(R"(GET /tiles/(\d+)/(\d+)/(\d+)\.png)");
    QRegularExpressionMatch match = re.match(QString(request));

    if (!match.hasMatch()) {
        qWarning() << "Unrecognised tile request:" << request.left(200);
        socket->write("HTTP/1.1 400 Bad Request\r\n"
                      "Content-Length: 0\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    const int z = match.captured(1).toInt();
    const int x = match.captured(2).toInt();
    const int y = match.captured(3).toInt();

    const QByteArray tile = this->renderTile(z, x, y);

    if (tile.isEmpty()) {
        // Tile is outside the ortho bounds — send transparent/empty response
        // 204 tells the map not to retry this tile
        socket->write("HTTP/1.1 204 No Content\r\n"
                      "Content-Length: 0\r\n\r\n");
    } else {
        const QByteArray header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/png\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: max-age=3600\r\n"
            "Content-Length: " + QByteArray::number(tile.size()) + "\r\n"
            "\r\n";
        socket->write(header);
        socket->write(tile);
    }

    socket->disconnectFromHost();
}

QByteArray BackendController::renderTile(int z, int x, int y)
{
    const QString key = QString("%1/%2/%3").arg(z).arg(x).arg(y);

    // Check cache first
    {
        QMutexLocker lock(&this->cache_mutex_);
        if (this->tile_cache_.contains(key))
            return *this->tile_cache_.object(key);
    }

    // Cache miss — render via GDAL
    QByteArray tile = this->renderTileFromGDAL(z, x, y);

    // Cache the result — even empty tiles, to avoid re-hitting GDAL
    // for out-of-bounds requests
    {
        QMutexLocker lock(&this->cache_mutex_);
        this->tile_cache_.insert(key, new QByteArray(tile));
    }

    return tile;
}

QByteArray BackendController::renderTileFromGDAL(int z, int x, int y)
{

    GDALDataset* ds = this->acquireDataset();
    if (!ds) return {};
    
    // if (!this->gdal_dataset_)
    //     return {};

    const int TILE = 256;

    // --- 1. Convert XYZ tile to Web Mercator bounds ---
    const double PI = M_PI;
    const double n  = std::pow(2.0, z);

    const double lonWest  =  (double)x       / n * 360.0 - 180.0;
    const double lonEast  =  (double)(x + 1) / n * 360.0 - 180.0;
    const double latNorth = std::atan(std::sinh(PI * (1.0 - 2.0 * (double)y       / n))) * 180.0 / PI;
    const double latSouth = std::atan(std::sinh(PI * (1.0 - 2.0 * (double)(y + 1) / n))) * 180.0 / PI;

    auto lonToMercX = [](double lon) {
        return lon * 20037508.342789244 / 180.0;
    };
    auto latToMercY = [&PI](double lat) {
        return std::log(std::tan(PI / 4.0 + lat * PI / 360.0))
               * 20037508.342789244 / PI;
    };

    const double tileMinX = lonToMercX(lonWest);
    const double tileMaxX = lonToMercX(lonEast);
    const double tileMinY = latToMercY(latSouth);
    const double tileMaxY = latToMercY(latNorth);

    // --- 2. Get VRT extent ---
    double gt[6];
    ds->GetGeoTransform(gt);

    const int    dsW    = ds->GetRasterXSize();
    const int    dsH    = ds->GetRasterYSize();
    const double dsMinX = gt[0];
    const double dsMaxX = gt[0] + dsW  * gt[1];
    const double dsMaxY = gt[3];
    const double dsMinY = gt[3] + dsH  * gt[5];  // gt[5] is negative

    // --- 3. Early exit if no intersection ---
    if (tileMaxX <= dsMinX || tileMinX >= dsMaxX ||
        tileMaxY <= dsMinY || tileMinY >= dsMaxY)
        return {};

    // --- 4. Compute intersection in VRT pixel space ---
    const double interMinX = std::max(tileMinX, dsMinX);
    const double interMaxX = std::min(tileMaxX, dsMaxX);
    const double interMinY = std::max(tileMinY, dsMinY);
    const double interMaxY = std::min(tileMaxY, dsMaxY);

    // Pixel coordinates within the VRT
    const double pxPerMeterX =  dsW / (dsMaxX - dsMinX);
    const double pxPerMeterY =  dsH / (dsMaxY - dsMinY);

    const int srcX = std::max(0, (int)std::floor((interMinX - dsMinX) * pxPerMeterX));
    const int srcY = std::max(0, (int)std::floor((dsMaxY - interMaxY) * pxPerMeterY));
    const int srcW = std::max(1, std::min((int)std::ceil((interMaxX - interMinX) * pxPerMeterX), dsW - srcX));
    const int srcH = std::max(1, std::min((int)std::ceil((interMaxY - interMinY) * pxPerMeterY), dsH - srcY));

    // --- 5. Compute destination sub-region in 256x256 tile ---
    const double tileW = tileMaxX - tileMinX;
    const double tileH = tileMaxY - tileMinY;

    const int dstX = std::max(0, (int)std::floor((interMinX - tileMinX) / tileW * TILE));
    const int dstY = std::max(0, (int)std::floor((tileMaxY - interMaxY) / tileH * TILE));
    const int dstW = std::max(1, std::min((int)std::ceil((interMaxX - interMinX) / tileW * TILE), TILE - dstX));
    const int dstH = std::max(1, std::min((int)std::ceil((interMaxY - interMinY) / tileH * TILE), TILE - dstY));

    // --- 6. RasterIO — GDAL selects correct overview automatically ---
    // This is the key benefit of the Warped VRT: one simple read call,
    // GDAL handles reprojection and overview selection internally
    const int bandCount = std::min(ds->GetRasterCount(), 4);

    std::vector<uint8_t> subBuffer(dstW * dstH * 4, 0);

    // Always read into RGBA layout
    // int bandMap[4] = {1, 2, 3, bandCount == 4 ? 4 : 0};

    CPLErr err = ds->RasterIO(
        GF_Read,
        srcX, srcY,         // Source offset in VRT pixel space
        srcW, srcH,         // Source region size — VRT reprojects on the fly
        subBuffer.data(),
        dstW, dstH,         // Scale to destination — VRT picks right overview
        GDT_Byte,
        bandCount >= 3 ? 3 : bandCount,  // Read RGB(A) only
        nullptr,            // nullptr = read bands 1,2,3 in order
        4,                  // Pixel stride
        dstW * 4,           // Line stride
        1                   // Band stride
    );

    if (err != CE_None)
        return {};

    // Fill alpha if source is RGB only
    if (bandCount == 3) {
        for (int i = 0; i < dstW * dstH; i++)
            subBuffer[i * 4 + 3] = 255;
    }

    // --- 7. Blit into full transparent 256x256 tile ---
    std::vector<uint8_t> outPixels(TILE * TILE * 4, 0);

    for (int row = 0; row < dstH; row++) {
        std::memcpy(
            outPixels.data() + ((dstY + row) * TILE + dstX) * 4,
            subBuffer.data() +  (row         * dstW)        * 4,
            dstW * 4
        );
    }

    // --- 8. Encode to PNG ---
    QImage img(outPixels.data(), TILE, TILE,
               TILE * 4, QImage::Format_RGBA8888);

    QByteArray out;
    QBuffer    buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    this->releaseDataset(ds);

    return out;
}

GDALDataset* BackendController::acquireDataset()
{
    QMutexLocker lock(&this->dataset_mutex_);
    if (!this->dataset_pool_.isEmpty())
        return this->dataset_pool_.takeLast();

     QByteArray bytes = this->cog_path_.toUtf8();

    // Open a fresh handle to the same VRT/COG
    return (GDALDataset*)GDALOpen(bytes.constData(), GA_ReadOnly);
}

void BackendController::releaseDataset(GDALDataset* ds)
{
    QMutexLocker lock(&this->dataset_mutex_);
    this->dataset_pool_.append(ds);
}

void BackendController::cleanupPreviousSession()
{
    // Must be called from main thread
    if (this->qtcp_server_) {
        this->qtcp_server_->close();
        this->qtcp_server_->deleteLater();
        this->qtcp_server_ = nullptr;
    }

    if (this->gdal_dataset_) {
        GDALClose(this->gdal_dataset_);
        this->gdal_dataset_ = nullptr;
    }

    {
        QMutexLocker lock(&this->cache_mutex_);
        this->tile_cache_.clear();
    }

    // Clean up temp files from previous session
    QStringList tmpFiles = {
        QDir::tempPath() + "/ortho_3857.tif",
        QDir::tempPath() + "/ortho_cog.tif"
    };
    for (const QString& f : tmpFiles) {
        if (QFile::exists(f))
            QFile::remove(f);
    }

    this->ortho_tile_url_.clear();
}

void BackendController::setOrthoProgress(double prog, const QString& status)
{
    this->ortho_progress_ = prog;
    this->ortho_status_   = status;
    // Marshal back to main thread for QML
    QMetaObject::invokeMethod(this, [this]() {
        emit orthoProgressChanged();
        emit orthoStatusChanged();
    }, Qt::QueuedConnection);
}

void BackendController::send_ack(AckType type)
{

    mavlink_msg_ack_t ack_msg = {};
    ack_msg.ack_type = static_cast<uint8_t>(type);

    // Send to ALL connected vehicles
    MultiVehicleManager* mgr = MultiVehicleManager::instance();
    for (int i = 0; i < mgr->vehicles()->count(); i++) {
        Vehicle* vehicle = mgr->vehicles()->value<Vehicle*>(i);
        if (!vehicle) continue;

        SharedLinkInterfacePtr sharedLink = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (!sharedLink){
            continue;
        }

        mavlink_message_t mavMsg;
        mavlink_msg_msg_ack_encode_chan(
            static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId()),
            static_cast<uint8_t>(MAVLinkProtocol::getComponentId()),
            sharedLink->mavlinkChannel(),
            &mavMsg,
            &ack_msg
        );

        qDebug() << "Sending Acknowledge message to system id " << vehicle->id(); 

        (void) vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), mavMsg);
        break; //send only one message out as it's going to both mavlink-routers on port 50882
    }
}

void BackendController::startMission()
{
    // qDebug() << "Start Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_start);
    this->sendStartMission(mission_state);
}

void BackendController::resumeMission()
{
    // qDebug() << "Resume Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_resume);
    this->sendStartMission(mission_state);  
}

void BackendController::endMission()
{
    // qDebug() << "End Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_end);
    this->sendStartMission(mission_state); 
}

void BackendController::startScan()
{
    // qDebug() << "Start Scan Button Pressed";
    scan_state_ = StartScan::scan_on;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_on);
    this->sendStartScan(scan_state);
}

void BackendController::stopScan()
{
    // qDebug() << "Stop Scan Button Pressed";
    scan_state_ = StartScan::scan_off;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_off);
    this->sendStartScan(scan_state);
}

void BackendController::emTubeSeasoning()
{
    // qDebug() << "Tube Seasoning Button Pressed";
    scan_state_ = StartScan::scan_tube_season;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_tube_season);
    this->sendStartScan(scan_state);
}

void BackendController::killScan()
{
    // qDebug() << "Kill Scan Button Pressed";
    scan_state_ = StartScan::scan_hard_kill;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_hard_kill);
    this->sendStartScan(scan_state);
}

void BackendController::payloadCal()
{
    // qDebug() << "Payload Calibration Button Pressed";
    scan_state_ = StartScan::scan_cal;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_cal);
    this->sendStartScan(scan_state);
    // this->calMsgSent_ = true;
    // this->uav_state_updated_.store(true);
}

//payload
void BackendController::setCadence(const uint32_t telemCadence)
{
    if (this->em_telem_cadence_ms_ != telemCadence) {
        this->em_telem_cadence_ms_ = telemCadence;
        emit cadenceChanged();
    }
}

void BackendController::setTestDuration(const uint32_t testDuration)
{
    if(this->em_test_duration_ms_ != testDuration)
    {
        this->em_test_duration_ms_ = testDuration;
        emit testDurationChanged();
    }
}

void BackendController::setExposure(const uint32_t xrayExposure)
{
    if(this->em_xray_exposure_ms_ != xrayExposure)
    {
        this->em_xray_exposure_ms_ = xrayExposure;
        emit exposureChanged();
    }
}

void BackendController::setVoltage(const uint32_t xrayVoltage)
{
    if(this->em_xray_voltage_kV_ != xrayVoltage)
    {
        this->em_xray_voltage_kV_ = xrayVoltage;
        emit voltageChanged();
    }
}

void BackendController::setCurrent(const uint32_t xrayCurrent)
{
    if(this->em_xray_current_uA_ != xrayCurrent)
    {
        this->em_xray_current_uA_ = xrayCurrent;
        emit currentChanged();
    }
}

void BackendController::setCommTimeout(const uint32_t commTimeout)
{
    if(this->em_xray_comms_block_timout_ms_ != commTimeout)
    {
        this->em_xray_comms_block_timout_ms_ = commTimeout;
        emit commTimeoutChanged();
    }
}

void BackendController::setXrayWindow(const uint64_t xrayWindow)
{
    if(this->det_xray_window_ms_ != xrayWindow)
    {
        this->det_xray_window_ms_ = xrayWindow;
        emit xrayWindowChanged();
    }
}

void BackendController::setFileName(const QString fileName)
{
    if(this->file_name_ != fileName)
    {
        this->file_name_ = fileName;
        emit fileNameChanged();
    }
}

void BackendController::setNumImages(const uint8_t numImages)
{
    if(this->num_images_ != numImages)
    {
        this->num_images_ = numImages;
        emit numImagesChanged();
    }
}

void BackendController::setEmitterConnected(const bool emitterConn)
{
    if(this->emConn_ != emitterConn)
    {
        this->emConn_ = emitterConn;
        emit emitterConnectionChanged();
    }
}

void BackendController::setDetectorConnected(const bool detectorConn)
{
    if(this->detConn_ != detectorConn)
    {
        this->detConn_ = detectorConn;
        emit detectorConnectionChanged();
    }
}

void BackendController::setFlightStatus(const QString flightstatus)
{
    if(this->flight_status_ != flightstatus)
    {
        this->flight_status_ = flightstatus;
        emit flightStatusChanged();
    }
}