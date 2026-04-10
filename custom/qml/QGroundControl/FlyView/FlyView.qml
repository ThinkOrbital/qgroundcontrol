import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap
import QGroundControl.Viewer3D

Item {
    id: _root

    readonly property bool _is3DMode: QGCViewer3DManager.displayMode === QGCViewer3DManager.View3D

    // These should only be used by MainRootWindow
    property var planController:    _planController
    property var guidedController:  _guidedController

    PlanMasterController {
        id:                     _planController
        flyView:                true
        Component.onCompleted:  start()
    }

    property bool   _mainWindowIsMap:       mapControl.pipState.state === mapControl.pipState.fullState
    property bool   _isFullWindowItemDark:  _mainWindowIsMap ? mapControl.isSatelliteMap : true
    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _missionController:     _planController.missionController
    property var    _geoFenceController:    _planController.geoFenceController
    property var    _rallyPointController:  _planController.rallyPointController
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property var    _guidedController:      guidedActionsController
    property var    _guidedValueSlider:     guidedValueSlider
    property var    _widgetLayer:           widgetLayer
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property rect   _centerViewport:        Qt.rect(0, 0, width, height)
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30
    property var    _mapControl:            mapControl
    property real   _widgetMargin:          ScreenTools.defaultFontPixelWidth * 0.75

    property real   _fullItemZorder:    0
    property real   _pipItemZorder:     QGroundControl.zOrderWidgets

    function _calcCenterViewPort() {
        var newToolInset = Qt.rect(0, 0, width, height)
        toolstrip.adjustToolInset(newToolInset)
    }

    function dropMainStatusIndicatorTool() {
        toolbar.dropMainStatusIndicatorTool();
    }

    QGCToolInsets {
        id:                     _toolInsets
        topEdgeLeftInset:       toolbar.height
        topEdgeCenterInset:     topEdgeLeftInset
        topEdgeRightInset:      topEdgeLeftInset
        leftEdgeBottomInset:    _pipView.leftEdgeBottomInset
        bottomEdgeLeftInset:    _pipView.bottomEdgeLeftInset
    }

    Item {
        id:                 mapHolder
        anchors.fill:       parent

        FlyViewMap {
            id:                     mapControl
            planMasterController:   _planController
            rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
            pipView:                _pipView
            pipMode:                !_mainWindowIsMap
            toolInsets:             customOverlay.totalToolInsets
            mapName:                "FlightDisplayView"
            enabled:                !_is3DMode
            visible:                !_is3DMode
        
            property var _vehicleDetector: null
            property var _vehicleEmitter: null

            Connections {
                target: QGroundControl.multiVehicleManager

                function onVehicleAdded(vehicle) {
                    console.log("Vehicle added, sysid:", vehicle.id)
                    if(vehicle.id === 1)
                        mapControl._vehicleDetector = vehicle

                    else if(vehicle.id === 2)
                        mapControl._vehicleEmitter = vehicle

                }

                function onVehicleRemoved(vehicle) {
                    console.log("Vehicle removed, sysid:", vehicle.id)
                    if(mapControl._vehicleEmitter === vehicle)
                        mapControl._vehicleEmitter = null

                    if(mapControl._vehicleDetector === vehicle)
                        mapControl._vehicleDetector = null
                }
            }

            MapCircle {
                center: backend.centerCoordinate
                radius: backend.sepDistance / 2
                color: "#4000FF00"
                border.width: 2
                border.color: "#00FF00"
            }

            MapCircle {
                center: mapControl._vehicleEmitter ? mapControl._vehicleEmitter.coordinate : QtPositioning.coordinate()
                radius: 30
                //color: "#4000FF00"
                border.width: 2
                border.color: "#e1ff00"
            }

            MapCircle {
                center: mapControl._vehicleEmitter ? mapControl._vehicleEmitter.coordinate : QtPositioning.coordinate()
                radius: 50
                //color: "#4000FF00"
                border.width: 2
                border.color: "#00FF00"
            }

            // --- Black dot in the center ---
            MapQuickItem {
                id: centerDot
                objectName: "centerDot"
                coordinate: backend.centerCoordinate
                anchorPoint.x: 10
                anchorPoint.y: 10
                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    color: "black"
                    radius: width / 2

                    Text {
                        anchors.centerIn: parent
                        text: "C"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                }
            }

            // --- Emitter  Goal ---
            MapQuickItem {
                id: emitterGoalDot
                objectName: "emitterGoalDot"
                coordinate: backend.emitterGoalCoord
                anchorPoint.x: 10
                anchorPoint.y: 10
                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    color: "green"
                    radius: width / 2

                    Text {
                        anchors.centerIn: parent
                        text: "EG"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                }
            }

            // --- Detector Goal ---
            MapQuickItem {
                id: detectorGoalDot
                objectName: "detectorGoalDot"
                coordinate: backend.detectorGoalCoord
                anchorPoint.x: 10
                anchorPoint.y: 10
                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    color: "green"
                    radius: width / 2

                    Text {
                        anchors.centerIn: parent
                        text: "DG"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                }
            }

        
            // --- Emitter  Position ---
            MapQuickItem {
                id: emitterPosition
                objectName: "emitterPosition"
                visible: mapControl._vehicleEmitter !== null
                coordinate: mapControl._vehicleEmitter ? mapControl._vehicleEmitter.coordinate : QtPositioning.coordinate()
                anchorPoint.x: 10
                anchorPoint.y: 10
                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    color: "blue"
                    radius: width / 2

                    Text {
                        anchors.centerIn: parent
                        text: "EP"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                }
            }

            // --- Detector  Position ---
            MapQuickItem {
                id: detectorPosition
                objectName: "detectorPosition"
                visible: mapControl._vehicleDetector !== null
                coordinate: mapControl._vehicleDetector ? mapControl._vehicleDetector.coordinate : QtPositioning.coordinate()
                anchorPoint.x: 10
                anchorPoint.y: 10
                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    color: "blue"
                    radius: width / 2

                    Text {
                        anchors.centerIn: parent
                        text: "DP"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                }
            }
        
            //Double click for circle
            onMapDoubleClicked: (position) => {
                const coord = toCoordinate(position, false)
                backend.setCenterCoordinate(coord)
                myCircle.center = coord
                followGps = false
                console.log("Double-click moved circle to", coord.latitude, coord.longitude)
            }
        
        
        
        }

        FlyViewVideo {
            id:         videoControl
            pipView:    _pipView
        }

        PipView {
            id:                     _pipView
            anchors.left:           parent.left
            anchors.bottom:         parent.bottom
            anchors.margins:        _toolsMargin
            item1IsFullSettingsKey: "MainFlyWindowIsMap"
            item1:                  mapControl
            item2:                  QGroundControl.videoManager.hasVideo ? videoControl : null
            show:                   QGroundControl.videoManager.hasVideo && !QGroundControl.videoManager.fullScreen &&
                                        (videoControl.pipState.state === videoControl.pipState.pipState || mapControl.pipState.state === mapControl.pipState.pipState)
            z:                      QGroundControl.zOrderWidgets

            property real leftEdgeBottomInset: visible ? width + anchors.margins : 0
            property real bottomEdgeLeftInset: visible ? height + anchors.margins : 0
        }

        FlyViewWidgetLayer {
            id:                     widgetLayer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            anchors.margins:        _widgetMargin
            anchors.topMargin:      toolbar.height + _widgetMargin
            z:                      _fullItemZorder + 2
            parentToolInsets:       _toolInsets
            mapControl:             _mapControl
            visible:                !QGroundControl.videoManager.fullScreen
        }

        FlyViewCustomLayer {
            id:                 customOverlay
            anchors.fill:       widgetLayer
            z:                  _fullItemZorder + 2
            parentToolInsets:   widgetLayer.totalToolInsets
            mapControl:         _mapControl
            visible:            !QGroundControl.videoManager.fullScreen
        }

        // Development tool for visualizing the insets for a paticular layer, show if needed
        FlyViewInsetViewer {
            id:                     widgetLayerInsetViewer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            z:                      widgetLayer.z + 1
            insetsToView:           widgetLayer.totalToolInsets
            visible:                false
        }

        GuidedActionsController {
            id:                 guidedActionsController
            missionController:  _missionController
            guidedValueSlider:     _guidedValueSlider
        }

        //-- Guided value slider (e.g. altitude)
        GuidedValueSlider {
            id:                 guidedValueSlider
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            anchors.topMargin:  toolbar.height
            z:                  QGroundControl.zOrderTopMost
            visible:            false
        }

        Loader {
            id:             viewer3DLoader
            z:              1
            anchors.fill:   parent
            active:         _is3DMode

            onActiveChanged: {
                if (active) {
                    setSource("qrc:/qml/QGroundControl/Viewer3D/Models3D/Viewer3DModel.qml",
)
                }
            }
        }
    }

    FlyViewToolBar {
        id:                 toolbar
        guidedValueSlider:  _guidedValueSlider
        visible:            !QGroundControl.videoManager.fullScreen
    }
}
