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

        /*property var _activeTiles: ({})
        property var _tilePool:    []

        function lonToTileX(lon, z) {
            return Math.floor((lon + 180.0) / 360.0 * Math.pow(2, z))
        }

        function latToTileY(lat, z) {
            var latRad = lat * Math.PI / 180.0
            return Math.floor(
                (1.0 - Math.log(Math.tan(latRad) + 1.0 / Math.cos(latRad)) / Math.PI)
                / 2.0 * Math.pow(2, z))
        }

        function tileToCoord(tx, ty, tz) {
            var n      = Math.pow(2, tz)
            var lon    = tx / n * 360.0 - 180.0
            var latRad = Math.atan(Math.sinh(Math.PI * (1 - 2 * ty / n)))
            return QtPositioning.coordinate(latRad * 180.0 / Math.PI, lon)
        }

        function updateTiles() {
            if (!ortho.orthoReady) return
            if (!ortho.orthoTileUrl || ortho.orthoTileUrl === "") return
        
            var z      = Math.floor(mapControl.zoomLevel)
            var BUFFER = 1

            console.log("Zoom:", z)

            var nw = mapControl.toCoordinate(Qt.point(0, 0))
            var se = mapControl.toCoordinate(Qt.point(mapControl.width, mapControl.height))

            console.log("NW corner:", nw.latitude, nw.longitude)
            console.log("SE corner:", se.latitude, se.longitude)

            var xMin = lonToTileX(nw.longitude, z) - BUFFER
            var xMax = lonToTileX(se.longitude, z) + BUFFER
            var yMin = latToTileY(nw.latitude,  z) - BUFFER
            var yMax = latToTileY(se.latitude,  z) + BUFFER

            console.log("Tile range X:", xMin, "to", xMax)
            console.log("Tile range Y:", yMin, "to", yMax)

            // Manually verify tile for known dataset lon
            var datasetLon = -105.2265  // gt[0] converted to degrees
            var expectedX = Math.floor((datasetLon + 180.0) / 360.0 * Math.pow(2, z))
            console.log("Expected tile X for dataset west edge:", expectedX)
            console.log("NW corner tile X:", lonToTileX(nw.longitude, z))
            console.log("NW corner lon:", nw.longitude)

            var neededKeys = {}
            for (var tx = xMin; tx <= xMax; tx++) {
                for (var ty = yMin; ty <= yMax; ty++) {
                    neededKeys[z + "/" + tx + "/" + ty] = { z: z, x: tx, y: ty }
                }
            }

            // Return unneeded tiles to pool
            for (var key in _activeTiles) {
                if (!(key in neededKeys)) {
                    var old = _activeTiles[key]
                    old.visible = false
                    _tilePool.push(old)
                    delete _activeTiles[key]
                }
            }
            console.log("Total tiles to request:", Object.keys(neededKeys).length)
            // Assign or create tiles
            for (var k in neededKeys) {
                console.log("Requesting tile:", k)
                if (k in _activeTiles) continue

                var t   = neededKeys[k]
                var url = ortho.orthoTileUrl
                            .replace("{z}", t.z)
                            .replace("{x}", t.x)
                            .replace("{y}", t.y)

                var tileObj
                if (_tilePool.length > 0) {
                    tileObj          = _tilePool.pop()
                    tileObj.tileX    = t.x
                    tileObj.tileY    = t.y
                    tileObj.tileZ    = t.z
                    tileObj.imageUrl = url
                    tileObj.visible  = true
                } else {
                    tileObj = tileComponent.createObject(mapControl, {
                        "tileX": t.x, "tileY": t.y, "tileZ": t.z, "imageUrl": url
                    })
                }

                _activeTiles[k] = tileObj
            }
        }

        function clearTiles() {
            for (var key in _activeTiles) {
                _activeTiles[key].destroy()
            }
            _activeTiles = {}
            for (var i = 0; i < _tilePool.length; i++) {
                _tilePool[i].destroy()
            }
            _tilePool = []
        }

        Timer { id: panDebounce;  interval: 80;  onTriggered: mapHolder.updateTiles() }
        Timer { id: zoomDebounce; interval: 300; onTriggered: mapHolder.updateTiles() }

        Connections {
            target: mapControl 
            function onCenterChanged()    { panDebounce.restart()  }
            function onZoomLevelChanged() { zoomDebounce.restart() }
        }

        Connections {
            target: ortho
            function onOrthoReadyChanged() {
                if (ortho.orthoReady) mapHolder.updateTiles()
                else                    mapHolder.clearTiles()
            }
        }

        Component {
            id: tileComponent

            MapQuickItem {
                property int    tileX
                property int    tileY
                property int    tileZ
                property string imageUrl

                anchorPoint.x: 0
                anchorPoint.y: 0
                zoomLevel:     tileZ + 1
                coordinate:    mapHolder.tileToCoord(tileX, tileY, tileZ)

                sourceItem: Image {
                    source:   imageUrl
                    width:    256
                    height:   256
                    opacity:  ortho.orthoOpacity
                    visible:  ortho.orthoReady
                    cache:    true
                    fillMode: Image.Stretch

                    onStatusChanged: {
                        if (status === Image.Error)
                            visible = false
                    }
                }
            }
        }*/

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
        
            property var _vehicleDetector: QGroundControl.multiVehicleManager.getVehicleById(1)
            property var _vehicleEmitter: QGroundControl.multiVehicleManager.getVehicleById(2)

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
