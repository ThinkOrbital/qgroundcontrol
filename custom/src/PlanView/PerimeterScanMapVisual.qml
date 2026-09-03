import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap
import QGroundControl.PlanView

/// \brief Map visuals for PerimeterScanComplexItem.
///
/// Shows an interactive, editable polygon using the standard QGCMapPolygonVisuals
/// component, which automatically provides the full Polygon Tools toolbar
/// (Basic, Circular, Trace, Load KML, …) when the item is selected.
///
/// A flight-path polyline traces the perimeter in waypoint order so the user
/// can see the direction of travel.

Item {
    id: _root

    property var  map
    property var  vehicle
    property bool interactive: true

    signal clicked(int sequenceNumber)

    property var  _missionItem: object
    property var  _polygon: object.offsetPolygon
    property var _polyline: object.corridorPolyline
    property bool _currentItem: object.isCurrentItem

    property var _customSettings: QGroundControl.corePlugin.customSettings
    property var _sepDistFact:    _customSettings.separationDistance

    function _startWidthMarkers() {
        if (!_polyline || _polyline.count < 2 || !_sepDistFact) return []

        var startPt   = _polyline.path[0]
        var heading   = startPt.azimuthTo(_polyline.path[_polyline.count - 1])
        var halfWidth = _sepDistFact.value / 2
        var swapped   = _missionItem.swapUavs

        return [
            { coordinate: startPt.atDistanceAndAzimuth(halfWidth, heading + 90), 
                label: swapped ? qsTr("E") : qsTr("D") },
            { coordinate: startPt.atDistanceAndAzimuth(halfWidth, heading - 90), 
                label: swapped ? qsTr("D") : qsTr("E") }
        ]
    }

    Repeater {
        model: _startWidthMarkers()

        delegate: MapQuickItem {
            id: startWidthMarker
            anchorPoint.x: sourceItem.width / 2
            anchorPoint.y: sourceItem.height / 2
            coordinate: modelData.coordinate

            sourceItem: Rectangle {
                width:  30
                height: 30
                radius: width/2
                color:  "green"
                border.color: "white"
                border.width: 1

                QGCLabel {
                    anchors.centerIn: parent
                    text:             modelData.label
                    color:            "white"
                    font.bold:        true
                }
            }

            Component.onCompleted:   map.addMapItem(startWidthMarker)
            Component.onDestruction: map.removeMapItem(startWidthMarker)
        }
    }

    Repeater {
        model: (_polyline && _polyline.count > 1) ? [
            { coordinate: _polyline.path[0], label: qsTr("Start") },
            { coordinate: _polyline.path[_polyline.count - 1], label: qsTr("End") }
        ] : []

        delegate: MapQuickItem {
            id: endpointLabel
            anchorPoint.x: sourceItem.width / 2
            anchorPoint.y: sourceItem.height + 8   // pushes the text above the point instead of centering on it
            coordinate: modelData.coordinate

            sourceItem: QGCLabel {
                text:           modelData.label
                font.bold:      true
                font.pixelSize: 20
                color:          "white"
                style:          Text.Outline
                styleColor:     "black"
            }

            Component.onCompleted:   map.addMapItem(endpointLabel)
            Component.onDestruction: map.removeMapItem(endpointLabel)
        }
    }

    // --- Black dot in the center ---
    MapQuickItem {
        id: centerDot
        objectName: "centerDot"
        coordinate: _missionItem.centerCoordinate
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

        Component.onCompleted:   map.addMapItem(centerDot)
        Component.onDestruction: map.removeMapItem(centerDot)
    }

    // ----- polyline editing visuals (provides the Polyline Tools toolbar) -----
    QGCMapPolylineVisuals {
        id: perimeterScanPolylineVisuals
        mapControl: map
        mapPolyline: _polyline
        interactive: _currentItem && _root.interactive
        lineWidth: 3
        lineColor: "#be781c"
        opacity: _root.opacity
        visible: true
    }

    QGCMapPolygonVisuals {
        mapControl:      map
        mapPolygon:      _polygon
        interactive:     false
        borderWidth:     3
        borderColor:     "#ffffff"
        visible:         _polygon && _polygon.count > 2
    }

    QGCDynamicObjectManager { id: objMgr }

    Connections {
        target: _polyline
        function onPathChanged() {
            console.log("QML: _polyline.pathChanged fired, vertex count =", _polyline.count)
        }
    }

    // Connections {
    //     target: _polygon
    //     function onPathChanged() {
    //         console.log("PerimeterScanMapVisual: _polygon.pathChanged fired, count =", _polygon.count)
    //     }
    // }

    // Component.onCompleted: {
    //     console.log("PerimeterScanMapVisual: onCompleted, _root.interactive =", interactive,
    //                  " _currentItem =", _currentItem,
    //                  " combined (interactive for polyline) =", (_currentItem && _root.interactive))
    //     console.log("PerimeterScanMapVisual: _polygon at startup =", _polygon,
    //                     " count =", _polygon.count)

    // }

    Component.onDestruction: {
        objMgr.destroyObjects()
    }
}
