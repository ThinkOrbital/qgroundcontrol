import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import Custom.Widgets

import QGroundControl.FlightMap 1.0
import QGroundControl.FactControls

Item {
    property var parentToolInsets                       // These insets tell you what screen real estate is available for positioning the controls in your overlay
    property var totalToolInsets:   _totalToolInsets    // The insets updated for the custom overlay additions
    property var mapControl

    readonly property string noGPS:         qsTr("NO GPS")
    readonly property real   indicatorValueWidth:   ScreenTools.defaultFontPixelWidth * 7

    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _multiVehicleManager:   QGroundControl.multiVehicleManager
    property real   _indicatorDiameter:     ScreenTools.defaultFontPixelWidth * 18
    property real   _indicatorsHeight:      ScreenTools.defaultFontPixelHeight
    property var    _sepColor:              qgcPal.globalTheme === QGCPalette.Light ? Qt.rgba(0,0,0,0.5) : Qt.rgba(1,1,1,0.5)
    property color  _indicatorsColor:       qgcPal.text
    property bool   _isVehicleGps:          _activeVehicle ? _activeVehicle.gps.count.rawValue > 1 && _activeVehicle.gps.hdop.rawValue < 1.4 : false
    property string _altitude:              _activeVehicle ? (isNaN(_activeVehicle.altitudeRelative.value) ? "0.0" : _activeVehicle.altitudeRelative.value.toFixed(1)) + ' ' + _activeVehicle.altitudeRelative.units : "0.0"
    property string _distanceStr:           isNaN(_distance) ? "0" : _distance.toFixed(0) + ' ' + QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
    property real   _heading:               _activeVehicle   ? _activeVehicle.heading.rawValue : 0
    property real   _distance:              _activeVehicle ? _activeVehicle.distanceToHome.rawValue : 0
    property string _messageTitle:          ""
    property string _messageText:           ""
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property var _vehicleDetector: QGroundControl.multiVehicleManager.vehicles.count > 0
        ? QGroundControl.multiVehicleManager.getVehicleById(1)
        : null
    property var _vehicleEmitter:  QGroundControl.multiVehicleManager.vehicles.count > 0
        ? QGroundControl.multiVehicleManager.getVehicleById(2)
        : null
    property bool _detectorCanArm: _vehicleDetector
        ? (_vehicleDetector.healthAndArmingCheckReport.supported
            ? _vehicleDetector.healthAndArmingCheckReport.canArm
            : !_vehicleDetector.prearmError)
        : false

    property bool _emitterCanArm: _vehicleEmitter
        ? (_vehicleEmitter.healthAndArmingCheckReport.supported
            ? _vehicleEmitter.healthAndArmingCheckReport.canArm
            : !_vehicleEmitter.prearmError)
        : false
    property bool _vehiclesReadyForMission: _detectorCanArm && _emitterCanArm

    property var  _customSettings:  QGroundControl.corePlugin.customSettings
    property var _sepDistFact: _customSettings.separationDistance
    property var _bearingFact: _customSettings.bearing
    property var _goalAltitudeFact: _customSettings.altitude
    property var _detOffsetFact: _customSettings.detOffset
    property var _emAltOffsetFact: _customSettings.emAltOffset
    property var _flightAltFact: _customSettings.flightAlt
    property var _flightVelFact: _customSettings.flightVel
    property var _goalLatFact: _customSettings.goalLat
    property var _goalLonFact: _customSettings.goalLon
    property var _numImagesFact: _customSettings.numImages
    property var _fileNameFact: _customSettings.fileName
    property var _detectorXrayWindowFact: _customSettings.detectorXrayWindow


    function secondsToHHMMSS(timeS) {
        var sec_num = parseInt(timeS, 10);
        var hours   = Math.floor(sec_num / 3600);
        var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
        var seconds = sec_num - (hours * 3600) - (minutes * 60);
        if (hours   < 10) {hours   = "0"+hours;}
        if (minutes < 10) {minutes = "0"+minutes;}
        if (seconds < 10) {seconds = "0"+seconds;}
        return hours+':'+minutes+':'+seconds;
    }

    QGCToolInsets {
        id:                     _totalToolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        //leftEdgeCenterInset:    exampleRectangle.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        //rightEdgeBottomInset:   parent.width - compassBackground.x
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        // topEdgeCenterInset:     compassArrowIndicator.y + compassArrowIndicator.height
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        //bottomEdgeRightInset:   parent.height - attitudeIndicator.y
    }

    Rectangle {
        id: statusBar
        anchors.top: parent.top
        //anchors.topMargin: parentToolInsets.topEdgeLeftInset + _toolsMargin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: parentToolInsets.leftEdgeTopInset + _toolsMargin / 2
        anchors.rightMargin: parentToolInsets.rightEdgeTopInset + _toolsMargin / 2
        height: 32
        color: "#084f8a"
        border.color: "#444"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: backend.flightStatus
            color: "#ffffff"
            font.pixelSize: 16
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    QGCGroupBox {
        id: targetDropdown
        anchors.left: scanMissionButtons.right
        anchors.top: statusBar.bottom
        anchors.topMargin: _toolsMargin/2
        //anchors.leftMargin: parentToolInsets.leftEdgeTopInset + 2*_toolsMargin
        //width: parentToolInsets.leftEdgeTopInset - _toolsMargin

        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.4)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }

        ColumnLayout {
            /*
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: parentToolInsets.topEdgeLeftInset + 2*_toolsMargin
            anchors.leftMargin: _toolsMargin
            width: parentToolInsets.leftEdgeTopInset - _toolsMargin
            */
            
            // ================= TARGET INFO DROPDOWN ================
            // Header row
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    text:           (targetExpanded ? "▼ " : "▶ ") + "Target Info"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true

                    property bool targetExpanded: false

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    parent.targetExpanded = !parent.targetExpanded
                    }

                    id: targetHeader
                }
            }

            // Divider
            Rectangle {
                Layout.fillWidth: true
                height:           1
                color:            Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            }

            ColumnLayout {
                visible:          targetHeader.targetExpanded
                Layout.fillWidth: true
                spacing:          2

                // -------- LATITUDE --------
                QGCLabel {
                    text: "Target Latitude:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    text: backend.centerCoordinate.latitude.toFixed(7)
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    unitsLabel: "deg"
                    showUnits: true
                    onEditingFinished: backend.centerCoordinate =
                        QtPositioning.coordinate(
                            parseFloat(text),
                            backend.centerCoordinate.longitude
                        )
                }

                // -------- LONGITUDE --------
                QGCLabel {
                    text: "Target Longitude:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    text: backend.centerCoordinate.longitude.toFixed(7)
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    unitsLabel: "deg"
                    showUnits: true
                    onEditingFinished: backend.centerCoordinate =
                        QtPositioning.coordinate(
                            backend.centerCoordinate.latitude,
                            parseFloat(text)
                        )
                }

                // -------- Target ALTITUDE --------
                QGCLabel {
                    text: "Target Altitude:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    unitsLabel: "m"
                    showUnits: true
                    fact: _goalAltitudeFact
                }

                // -------- BEARING --------
                QGCLabel {
                    text: "UAV-Target Bearing (0-360):"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    unitsLabel: "deg"
                    showUnits: true
                    fact: _bearingFact
                }

                // -------- DETECTOR POS OFFSET --------
                QGCLabel {
                    text: "Detector-Target Separation:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    unitsLabel: "m"
                    showUnits: true
                    fact: _detOffsetFact
                }
            }
        }
    }

    QGCGroupBox {
        id: uavDropdown
        anchors.left: targetDropdown.right
        anchors.top: statusBar.bottom
        anchors.topMargin: _toolsMargin/2
        //anchors.leftMargin: parentToolInsets.leftEdgeTopInset + targetDropdown.width + 2*nudgeBox.width + 2*_toolsMargin
        //width: parentToolInsets.leftEdgeTopInset - _toolsMargin

        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.4)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }
        ColumnLayout{
            Layout.fillWidth: true
            // Header row
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    id:             uavHeader
                    text:           (uavExpanded ? "▼ " : "▶ ") + "UAV Info"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true

                    property bool uavExpanded: false

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    parent.uavExpanded = !parent.uavExpanded
                    }
                }
            }
            // ================= TARGET INFO DROPDOWN ================
            // Divider
            Rectangle {
                Layout.fillWidth: true
                height:           1
                color:            Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            }

            ColumnLayout {
                visible: uavHeader.uavExpanded
                Layout.fillWidth: true
                spacing: 2
            
            // -------- SEPARATION --------
            QGCLabel {
                text: "UAV-UAV Separation (m):"
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: false
            }

            FactTextField {
                Layout.fillWidth: true
                fact: _sepDistFact
                unitsLabel: "m"
                showUnits: true
            }

                // -------- EMITTER ALT OFFSET --------
                QGCLabel {
                    text: "Emitter Altitude offset:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    fact: _emAltOffsetFact
                    unitsLabel: "m"
                    showUnits: true
                }

                // -------- Flight ALTITUDE --------
                QGCLabel {
                    text: "UAV Flight Altitude:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    fact: _flightAltFact
                    unitsLabel: "m"
                    showUnits: true
                }

                // -------- Flight VELOCITY --------
                QGCLabel {
                    text: "UAV Flight Velocity:"
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: false
                }
                FactTextField {
                    Layout.fillWidth: true
                    //font.pointSize: ScreenTools.smallFontPointSize  // smaller font
                    fact: _flightVelFact
                    unitsLabel: "m/s"
                    showUnits: true
                }
            }   
        }
    }     


    QGCGroupBox {
        id: detectorDropdown
        anchors.left: uavDropdown.right
        anchors.top: statusBar.bottom
        anchors.topMargin: _toolsMargin/2
        //anchors.leftMargin: parentToolInsets.leftEdgeTopInset + uavDropdown.width + targetDropdown.width + emitterDropdown.width + 2*nudgeBox.width + 2*_toolsMargin
        //width: parentToolInsets.leftEdgeTopInset - _toolsMargin

        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.4)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }

        ColumnLayout{
            Layout.fillWidth: true
            
            // ================= DETECTOR DROPDOWN ================
            // Header row
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    id:             detectorHeader
                    text:           (detectorExpanded ? "▼ " : "▶ ") + "Payload Info"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true

                    property bool detectorExpanded: false

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    parent.detectorExpanded = !parent.detectorExpanded
                    }
                }
            }
            
            // Divider
            Rectangle {
                Layout.fillWidth: true
                height:           1
                color:            Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            }

            ColumnLayout {
                visible: detectorHeader.detectorExpanded
                Layout.fillWidth: true
                spacing: 2

                QGCLabel { text: "X-ray window" }
                QGCTextField {
                    Layout.fillWidth: true
                    unitsLabel: "ms"
                    showUnits: true
                    text: backend.detectorXrayWindow
                    onEditingFinished:
                        backend.detectorXrayWindow = text
                }

                QGCLabel {text: "Number of Images"}
                QGCTextField {
                    Layout.fillWidth: true
                    text: backend.numImages
                    onEditingFinished:
                        backend.numImages = text;
                }

                QGCLabel { text: "File Name"}
                QGCTextField {
                    Layout.fillWidth: true
                    text: backend.fileName
                    onEditingFinished:
                        backend.fileName = text;
                }
            }
        }
    }

    QGCGroupBox {
        id: emitterStatusDropdown
        anchors.left: detectorDropdown.right
        anchors.top: statusBar.bottom
        anchors.topMargin: _toolsMargin/2
        //anchors.rightMargin: _toolsMargin/2
        //width: parentToolInsets.leftEdgeTopInset - _toolsMargin

        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.6)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }
        
        ColumnLayout{
            Layout.fillWidth: true
            
            // ================= EMITTER STATUS DROPDOWN ================
            // Header row
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    id:             emStatusHeader
                    text:           (emStatusExpanded ? "▼ " : "▶ ") + "Emitter Status"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true

                    property bool emStatusExpanded: false

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    parent.emStatusExpanded = !parent.emStatusExpanded
                    }
                }
            }
            
            // Divider
            Rectangle {
                Layout.fillWidth: true
                height:           1
                color:            Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            }

            ColumnLayout{
                visible: emStatusHeader.emStatusExpanded
                Layout.fillWidth: true
                spacing: 2

                QGCLabel { text: "Model: " + backend.emModel }
                QGCLabel { text: "Serial: " + backend.emSerial }
                QGCLabel { text: "HV voltage (V): " + backend.HVVoltage }
                QGCLabel { text: "HV current (mA): " + backend.HVCurrent.toFixed(1) }
                QGCLabel { text: "Temperature (\u00B0C): " + backend.EmTemp.toFixed(1) }
                QGCLabel { text: "FIL current (mA): " + backend.FILCurrent }
                QGCLabel { text: "BAT voltage (V): " + backend.BATVoltage.toFixed(1) }
            }

        }
    }

    QGCGroupBox {
        id: detectorStatusDropdown
        anchors.left: emitterStatusDropdown.right
        anchors.top: statusBar.bottom
        anchors.topMargin: _toolsMargin/2


        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.6)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }

        ColumnLayout{
            Layout.fillWidth: true

            // ================= DETECTOR STATUS DROPDOWN ================
            // Header row
            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    id:             detStatusHeader
                    text:           (detStatusExpanded ? "▼ " : "▶ ") + "Detector Status"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold:      true

                    property bool detStatusExpanded: false

                    MouseArea {
                        anchors.fill: parent
                        onClicked:    parent.detStatusExpanded = !parent.detStatusExpanded
                    }
                }
            }
            
            // Divider
            Rectangle {
                Layout.fillWidth: true
                height:           1
                color:            Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            }

            ColumnLayout {
                visible: detStatusHeader.detStatusExpanded
                Layout.fillWidth: true
                spacing: 2

                QGCLabel { 
                    text: "Version: " + backend.detVer 
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                QGCLabel { text: "Integration Time (ms): " + backend.detInt }
                QGCLabel { text: "Battery Voltage (V): " + backend.detBatV.toFixed(3) + " (" + backend.detBatPerc + "%)" }
                QGCLabel { text: "External Power Present: " + backend.detBatExtPow}
                QGCLabel { text: "Detector Status: " + backend.detStatus }
            }
        }
    }

    QGCGroupBox{
        id: scanMissionButtons
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: parentToolInsets.topEdgeLeftInset + 2*_toolsMargin
        anchors.leftMargin: _toolsMargin/2

        background: Rectangle {
            color:        Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.2)  // 0.0 = fully transparent, 1.0 = fully opaque
            radius:       ScreenTools.defaultFontPixelWidth * 0.75
            border.color: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
            border.width: 1
        }

        ColumnLayout {

            QGCButton {
                Layout.fillWidth: true
                text: "Start Scan"
                onClicked: backend.startScan()
                enabled: backend.isStartScanButtonEn
            }

            QGCButton {
                Layout.fillWidth: true
                text: "Stop Scan"
                onClicked: backend.stopScan()
                enabled: backend.isStopScanButtonEn

                // Make button red
               /* background: Rectangle {
                    color: "red"
                    radius: 4
                    border.color: "darkred"
                    border.width: 1
                }

                // Optional: change text color to white for contrast
                contentItem: Text {
                    text: qsTr("Stop Scan")
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }*/
            }

            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Calibrate Payloads"
                onClicked: backend.payloadCal()
                enabled: _vehicleDetector && _vehicleEmitter && !_vehicleDetector.flying && !_vehicleEmitter.flying
            }

            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Tube Seasoning"
                onClicked: backend.emTubeSeasoning()
                enabled: _vehicleEmitter && !_vehicleEmitter.flying
            }

            QGCButton {
                Layout.fillWidth: true
                text: "Kill Emitter Power"
                onClicked: backend.killScan()
                
                // Make button red
                background: Rectangle {
                    color: "red"
                    radius: 4
                    border.color: "darkred"
                    border.width: 1
                }

                // Optional: change text color to white for contrast
                contentItem: Text {
                    text: qsTr("Kill Emitter Power")
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // ================= NUDGE CONTROLS =================
            GridLayout {
                columns: 3
                Layout.topMargin: 6
                Layout.alignment: Qt.AlignHCenter

                function triangle(drawFn) {
                    return Qt.createQmlObject(
                        'import QtQuick 2.15; Canvas { onPaint: { var c = getContext("2d"); drawFn(c); } }',
                        this)
                }

                // NORTH
                Item {
                    Layout.row: 0; Layout.column: 1
                    width: 60; height: 40
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var c = getContext("2d")
                            c.clearRect(0,0,width,height)
                            c.beginPath()
                            c.moveTo(width/2,0)
                            c.lineTo(0,height)
                            c.lineTo(width,height)
                            c.closePath()
                            c.fillStyle = "lightgray"
                            c.fill()
                            c.stroke()
                        }
                    }
                    Text { anchors.centerIn: parent; text: "N"; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: backend.nudge(0, parseFloat(nudgeInput.text)) }
                }

                // WEST
                Item {
                    Layout.row: 1; Layout.column: 0
                    width: 40; height: 60
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var c = getContext("2d")
                            c.clearRect(0,0,width,height)
                            c.beginPath()
                            c.moveTo(0,height/2)
                            c.lineTo(width,0)
                            c.lineTo(width,height)
                            c.closePath()
                            c.fillStyle = "lightgray"
                            c.fill()
                            c.stroke()
                        }
                    }
                    Text { anchors.centerIn: parent; text: "W"; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: backend.nudge(270, parseFloat(nudgeInput.text)) }
                }

                // CENTER
                ColumnLayout {
                    Layout.row: 1; Layout.column: 1
                    spacing: 2
                    QGCTextField {
                        id: nudgeInput
                        Layout.preferredWidth: 60
                        unitsLabel: "m"
                        showUnits: true
                        text: "0.5"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // EAST
                Item {
                    Layout.row: 1; Layout.column: 2
                    width: 40; height: 60
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var c = getContext("2d")
                            c.clearRect(0,0,width,height)
                            c.beginPath()
                            c.moveTo(width,height/2)
                            c.lineTo(0,0)
                            c.lineTo(0,height)
                            c.closePath()
                            c.fillStyle = "lightgray"
                            c.fill()
                            c.stroke()
                        }
                    }
                    Text { anchors.centerIn: parent; text: "E"; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: backend.nudge(90, parseFloat(nudgeInput.text)) }
                }

                // SOUTH
                Item {
                    Layout.row: 2; Layout.column: 1
                    width: 60; height: 40
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var c = getContext("2d")
                            c.clearRect(0,0,width,height)
                            c.beginPath()
                            c.moveTo(0,0)
                            c.lineTo(width,0)
                            c.lineTo(width/2,height)
                            c.closePath()
                            c.fillStyle = "lightgray"
                            c.fill()
                            c.stroke()
                        }
                    }
                    Text { anchors.centerIn: parent; text: "S"; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: backend.nudge(180, parseFloat(nudgeInput.text)) }
                }
            }   
            // ================= MISSION BUTTONS =================
  
            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                //implicitWidth: ScreenTools.defaultFontPixelWidth * 12
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Send Goal"
                onClicked: backend.sendGoal()
                enabled: backend.isSendGoalButtonEn && _vehicleEmitter && _vehicleDetector
                Component.onCompleted: {
                    background.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorYellow, 1.3) : qgcPal.button)
                    background.border.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorYellow, 1.6) : qgcPal.buttonBorder)
                }
            }

            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                //implicitWidth: ScreenTools.defaultFontPixelWidth * 12
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Resume Mission"
                onClicked: backend.resumeMission()
                enabled: backend.isResumeMissionButtonEn
                Component.onCompleted: {
                    background.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorGreen, 1.3) : qgcPal.button)
                    background.border.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorGreen, 1.6) : qgcPal.buttonBorder)
                }
            }
            
            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                //implicitWidth: ScreenTools.defaultFontPixelWidth * 12
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Start Mission"
                onClicked: backend.startMission()
                enabled: backend.isStartMissionButtonEn && _vehiclesReadyForMission
                Component.onCompleted: {
                    background.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorGreen, 1.3) : qgcPal.button)
                    background.border.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorGreen, 1.6) : qgcPal.buttonBorder)
                }
            }

            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                //implicitWidth: ScreenTools.defaultFontPixelWidth * 12
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "End Mission"
                onClicked: backend.endMission()
                enabled: backend.isEndMissionButtonEn
                Component.onCompleted: {
                    background.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorOrange, 1.3) : qgcPal.button)
                    background.border.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorOrange, 1.6) : qgcPal.buttonBorder)
                }           
            }
        }
    }

    Component.onCompleted: {
        backend.sepDistance = _sepDistFact.value
        backend.bearing = _bearingFact.value
        backend.altitude = _goalAltitudeFact.value
        backend.detOffset = _detOffsetFact.value
        backend.emAltOffset = _emAltOffsetFact.value
        backend.flightAlt = _flightAltFact.value
        backend.flightVel = _flightVelFact.value
        backend.goalLat = _goalLatFact.value
        backend.goalLon = _goalLonFact.value
        backend.numImages = _numImagesFact.value
        backend.fileName = _fileNameFact.value
        backend.detectorXrayWindow = _detectorXrayWindowFact.value
    }
    Connections {
        target: _sepDistFact

        function onValueChanged() {
            backend.sepDistance =
                _sepDistFact.value
        }
    }
    Connections {
        target: _bearingFact

        function onValueChanged() {
            backend.bearing =
                _bearingFact.value
        }
    }
    Connections {
        target: _goalAltitudeFact

        function onValueChanged() {
            backend.altitude =
                _goalAltitudeFact.value
        }
    }
    Connections {
        target: _detOffsetFact

        function onValueChanged() {
            backend.detOffset =
                _detOffsetFact.value
        }
    }
    Connections {
        target: _emAltOffsetFact

        function onValueChanged() {
            backend.emAltOffset =
                _emAltOffsetFact.value
        }
    }
    Connections {
        target: _flightAltFact

        function onValueChanged() {
            backend.flightAlt =
                _flightAltFact.value
        }
    }
    Connections {
        target: _flightVelFact

        function onValueChanged() {
            backend.flightVel =
                _flightVelFact.value
        }
    }
    Connections {
        target: _goalLatFact

        function onValueChanged() {
            backend.goalLat =
                _goalLatFact.value
        }
    }
    Connections {
        target: _goalLonFact

        function onValueChanged() {
            backend.goalLon =
                _goalLonFact.value
        }
    }
    Connections {
        target: _numImagesFact

        function onValueChanged() {
            backend.numImages =
                _numImagesFact.value
        }
    }
    Connections {
        target: _fileNameFact

        function onValueChanged() {
            backend.fileName =
                _fileNameFact.value
        }
    }
    Connections {
        target: _detectorXrayWindowFact

        function onValueChanged() {
            backend.detectorXrayWindow =
                _detectorXrayWindowFact.value
        }
    }
}
