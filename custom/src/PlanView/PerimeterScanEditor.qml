import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlightMap

/// \brief Editor panel for PerimeterScanComplexItem.
///
/// Appears in the right-hand mission item list when the item is selected.
/// The polyline tools toolbar on the map is provided automatically by
/// QGCMapPolylineVisuals — no extra wiring is needed here.

Rectangle {
    id:     _root
    height: visible ? (editorColumn.height + (_margin * 2)) : 0
    width:  availableWidth
    color:  qgcPal.windowShadeDark
    radius: _radius

    required property var  missionItem
    required property real availableWidth

    property real _margin:     ScreenTools.defaultFontPixelWidth / 2
    property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 10.5
    property real _radius:     ScreenTools.defaultFontPixelWidth / 2
    property var _customSettings:  QGroundControl.corePlugin.customSettings
    property var _sepDistFact: _customSettings.separationDistance
    property var _targetAltFact: _customSettings.targetAlt
    property var _emAltOffsetFact: _customSettings.emAltOffset
    property var _flightAltFact: _customSettings.flightAlt
    property var _flightVelFact: _customSettings.flightVel
    property var _detectorXrayWindowFact: _customSettings.detectorXrayWindow
    property var _fileNameFact: _customSettings.fileName
    property var _numImagesFact: _customSettings.numImages
    property var _overlapFact: _customSettings.overlap

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    ColumnLayout {
        id:             editorColumn
        anchors {
            top:    parent.top
            left:   parent.left
            right:  parent.right
            margins: _margin
        }
        spacing: _margin

        // Wizard hint – shown until the polyline is valid.
        QGCLabel {
            Layout.fillWidth:    true
            wrapMode:            Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text:                qsTr("Use the Polyline Tools to draw the linear scan target.")
            visible:             !missionItem.corridorPolyline.isValid
        }

        // Settings – shown once the polyline has been defined.
        GridLayout {
            Layout.fillWidth: true
            columnSpacing:    _margin
            rowSpacing:       _margin
            columns:          2
            visible:          missionItem.corridorPolyline.isValid

            QGCLabel { text: qsTr("UAV Flight Altitude") }
            FactTextField {
                fact: _flightAltFact
                Layout.preferredWidth: _fieldWidth
                showUnits: true
            }

            QGCLabel { text: qsTr("UAV-UAV Separation (m)") }
            FactTextField {
                fact: _sepDistFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("Target Altitude") }
            FactTextField {
                fact: _targetAltFact
                Layout.preferredWidth: _fieldWidth
                showUnits: true
            }

            QGCLabel { text: qsTr("Emitter Altitude offset") }
            FactTextField {
                fact: _emAltOffsetFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("Flight Velocity") }
            FactTextField {
                fact: _flightVelFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("X-ray window") }
            FactTextField {
                fact: _detectorXrayWindowFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("Number of Images") }
            FactTextField {
                fact: _numImagesFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("File Name") }
            FactTextField {
                fact: _fileNameFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "m"
                showUnits: true
            }

            QGCLabel { text: qsTr("Percent Overlap") }
            FactTextField {
                fact: _overlapFact
                Layout.preferredWidth: _fieldWidth
                unitsLabel: "%"
                showUnits: true
            }

            QGCButton {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                text: "Send Goal"
                onClicked: missionItem.sendLinearScanGoal()
                // TODO disable button using "dirty" syntax, if goal is sent gray it out. Ungray if params change.
                // enabled: backend.isSendGoalButtonEn && _vehicleEmitter && _vehicleDetector
                Component.onCompleted: {
                    background.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorYellow, 1.3) : qgcPal.button)
                    background.border.color = Qt.binding(() => enabled ? Qt.darker(qgcPal.colorYellow, 1.6) : qgcPal.buttonBorder)
                }
            }

        }
    }
}
