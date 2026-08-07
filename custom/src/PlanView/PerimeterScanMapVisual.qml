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

    Connections {
        target: _polygon
        function onPathChanged() {
            console.log("PerimeterScanMapVisual: _polygon.pathChanged fired, count =", _polygon.count)
        }
    }

    Component.onCompleted: {
        console.log("PerimeterScanMapVisual: onCompleted, _root.interactive =", interactive,
                     " _currentItem =", _currentItem,
                     " combined (interactive for polyline) =", (_currentItem && _root.interactive))
        console.log("PerimeterScanMapVisual: _polygon at startup =", _polygon,
                        " count =", _polygon.count)
    
    }

    Component.onDestruction: {
        objMgr.destroyObjects()
    }
}
