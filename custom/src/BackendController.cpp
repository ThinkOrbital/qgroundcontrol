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
                    this->send_ack(AckType::ack_coop);
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
                                this->setStartMissionButtonEn(true);
                            }

                            if(!targMsgSent_)
                            {
                                str_flight_status += "Waiting for user to send target information to UAVs. \n";
                            }

                            if(!calMsgSent_)
                            {
                                str_flight_status += "Waiting for user to calibrate panel. ";
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
                            str_flight_status = "Waiting for user to press start mission button.";
                        }

                        if(!targMsgSent_)
                        {
                            str_flight_status += "Waiting for user to send target information to UAVs. \n";
                        }

                        if(!calMsgSent_)
                        {
                            str_flight_status += "Waiting for user to calibrate panel. ";
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
                        case StartScan::scan_settings:
                            this->setFlightStatus("Updating Scan Settings...");
                            break;
                        case StartScan::scan_hard_kill:
                            this->setFlightStatus("Powering off emitter...");
                            break;
                        case StartScan::scan_cal:
                            this->setFlightStatus("Calibrating Detector...");
                            break;
                        case StartScan::scan_reset_cal:
                            this->setFlightStatus("Resetting Calibration...");
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
            this->setFlightStatus("Waiting for user to send target information to UAVs. \n Waiting for user to calibrate panel.");
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
    qDebug() << "Send Goal Button Pressed!";

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
    qDebug() << "Start Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_start);
    this->sendStartMission(mission_state);
}

void BackendController::resumeMission()
{
    qDebug() << "Resume Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_resume);
    this->sendStartMission(mission_state);  
}

void BackendController::endMission()
{
    qDebug() << "End Mission Button Pressed";
    uint8_t mission_state = static_cast<uint8_t>(StartMission::mission_end);
    this->sendStartMission(mission_state); 
}

void BackendController::startScan()
{
    qDebug() << "Start Scan Button Pressed";
    scan_state_ = StartScan::scan_on;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_on);
    this->sendStartScan(scan_state);
}

void BackendController::stopScan()
{
    qDebug() << "Stop Scan Button Pressed";
    scan_state_ = StartScan::scan_off;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_off);
    this->sendStartScan(scan_state);
}

void BackendController::emTubeSeasoning()
{
    qDebug() << "Tube Seasoning Button Pressed";
    scan_state_ = StartScan::scan_tube_season;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_tube_season);
    this->sendStartScan(scan_state);
}

void BackendController::killScan()
{
    qDebug() << "Kill Scan Button Pressed";
    scan_state_ = StartScan::scan_hard_kill;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_hard_kill);
    this->sendStartScan(scan_state);
}

void BackendController::detCal()
{
    qDebug() << "Detector Calibration Button Pressed";
    scan_state_ = StartScan::scan_cal;
    uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_cal);
    this->sendStartScan(scan_state);
    // this->calMsgSent_ = true;
    // this->uav_state_updated_.store(true);
}

// void BackendController::detResetCal()
// {
//     qDebug() << "Detector Reset Calibration Button Pressed";
//     scan_state_ = StartScan::scan_reset_cal;
//     uint8_t scan_state = static_cast<uint8_t>(StartScan::scan_reset_cal);
//     this->sendStartScan(scan_state);

// }

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