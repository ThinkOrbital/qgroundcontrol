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

        // Tile math functions — accessible by everything in mapHolder scope
        function tile2lat(y, z) {
            var n = Math.PI - 2 * Math.PI * y / Math.pow(2, z)
            return 180 / Math.PI * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
        }

        function tile2lon(x, z) {
            return x / Math.pow(2, z) * 360 - 180
        }

        function lon2tileX(lon, z) {
            console.log("z:", z, "lon:", lon)
            return Math.floor((lon + 180) / 360 * Math.pow(2, z))
        }

        function lat2tileY(lat, z) {
            console.log("z:", z, "lat:", lat)
            var rad = lat * Math.PI / 180
            console.log("rad:", rad)
            return Math.floor(
                (1 - Math.log(Math.tan(rad) + 1 / Math.cos(rad)) / Math.PI) / 2
                * Math.pow(2, z)
            )
        }

        function updateTiles() {

            if (!ortho || !ortho.orthoReady)
                return

            z = Math.floor(mapControl.zoomLevel)

            //zoom guard
            if (z < 16 || z > 22)
                return

            if (!mapControl.visibleRegion)
                return

            console.log("visibleRegion:", mapControl.visibleRegion)
            console.log("boundingGeoRectangle:", mapControl.visibleRegion ? mapControl.visibleRegion.boundingGeoRectangle() : "null")

            let rect = mapControl.visibleRegion.boundingGeoRectangle()

            let tx0 = lon2tileX(rect.topLeft.longitude, z)
            let tx1 = lon2tileX(rect.bottomRight.longitude, z)
            let ty0 = lat2tileY(rect.topLeft.latitude, z)
            let ty1 = lat2tileY(rect.bottomRight.latitude, z)

            console.log("raw tile bounds:", 
                        "tx", tx0, tx1,
                        "ty", ty0, ty1)

            let minX = Math.min(tx0, tx1)
            let maxX = Math.max(tx0, tx1)
            let minY = Math.min(ty0, ty1)
            let maxY = Math.max(ty0, ty1)

            console.log("tile bounds:",
                "X:", minX, maxX,
                "Y:", minY, maxY)

            console.log("zoom:", mapControl.zoomLevel)
            console.log("center:", mapControl.center)
            console.log("width/height:", mapControl.width, mapControl.height)

            if ((maxX - minX) * (maxY - minY) > 100)
                return

            tileModel.clear()

            console.log("ortho tile url is: ", ortho.orthoTileUrl)
            console.log("ortho object:", ortho)

            for (let x = minX; x <= maxX; x++) {
                for (let y = minY; y <= maxY; y++) {
                    tileModel.append({
                        x: x,
                        y: y,
                        z: z,
                        url: ortho.orthoTileUrl
                                .replace("{z}", z)
                                .replace("{x}", x)
                                .replace("{y}", y)
                    })
                }
            }

            //visibleTiles = tiles
            console.log("tiles count:", tileModel.count)
        }

        ListModel {
            id: tileModel
        }

        //orthomosiac zoom and map movement
        Connections {
            target: mapControl

            function onCenterChanged() { mapHolder.updateTiles() }
            function onZoomLevelChanged() { mapHolder.updateTiles() }
        }


        Connections {
            target: typeof ortho !== "undefined" ? ortho : null

            function onOrthoReadyChanged() {
                console.log("Ortho ready signal fired")
                if (ortho && ortho.orthoReady) {
                    mapHolder.updateTiles()
                }
            }
        }

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

    

            property int tileSize: 256

            Repeater {
                model: tileModel

                delegate: MapQuickItem {
                    Component.onCompleted: console.log("MapQuickItem created at", coordinate)
                    coordinate: QtPositioning.coordinate(
                        mapHolder.tile2lat(model.y, model.z),
                        mapHolder.tile2lon(model.x, model.z)
                    )
                    anchorPoint: Qt.point(0, 0)

                    sourceItem: Rectangle {
                        width: 256
                        height: 256
                        color: "red"
                        opacity: 0.8
                    }
                    /*sourceItem: Image {
                        source:      model.url
                        width:       256
                        height:      256
                        cache:       false
                        asynchronous: true
                        onStatusChanged: {
                            if (status === Image.Error)   visible = false
                            if (status === Image.Ready)   console.log("Tile loaded:", source)
                        }
                    }*/
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
