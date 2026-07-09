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

    // TODO temporary debug
    onInteractiveChanged: console.log("PerimeterScanMapVisual: _root.interactive changed ->", interactive)
    on_CurrentItemChanged: console.log("PerimeterScanMapVisual: _currentItem changed ->", _currentItem)

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

    // QGCMapPolygonVisuals {
    //     mapControl: map
    //     mapPolygon: _polygon
    //     interactive: false
    //     visible: _polygon && _polygon.count > 2
    // }
    QGCMapPolygonVisuals {
        mapControl:      map
        mapPolygon:      _polygon
        interactive:     false
        borderWidth:     3
        borderColor:     "#ffffff"
        // interiorColor:   "#00cc44"
        // interiorOpacity: 
        visible:         _polygon && _polygon.count > 2
    }

    // TransectStyleMapVisuals {
    //     polygonInteractive: false
    //     hideMapPolygon:     mapPolylineVisuals.dragging

    //     property bool _currentItem: object.isCurrentItem

    //     QGCMapPolylineVisuals {
    //         id:             mapPolylineVisuals
    //         mapControl:     map
    //         mapPolyline:    _polyline
    //         interactive:    _currentItem && _root.interactive
    //         lineWidth:      3 // TODO remove duplicate properties
    //         lineColor:      "#be781c"
    //         visible:        _currentItem   //-> Was currentItem, forcing visiblity now
    //         opacity:        _root.opacity
    //     }
    // }

    // ----- flight-path polyline (perimeter traversal order) ----------------
    // Shown only when the item is selected so the map stays uncluttered.
    // Component {
    //     id: flightPathComponent

    //     MapPolyline {
    //         line.color: "#00cc44"
    //         line.width: 2
    //         visible:    _currentItem
    //         opacity:    _root.opacity

    //         // Build the closed path from the polygon vertices.
    //         path: {
    //             const pts = _polygon.path
    //             if (pts.length < 2) return []
    //             // Append first vertex at the end to close the loop visually.
    //             return pts.concat([pts[0]])
    //         }
    //     }
    // }

    // ----- entry-point marker ----------------------------------------------
    // Component {
    //     id: entryPointComponent

    //     MapQuickItem {
    //         anchorPoint.x: sourceItem.width  / 2
    //         anchorPoint.y: sourceItem.height / 2
    //         coordinate:    _missionItem.coordinate
    //         visible:       _currentItem && _polygon.isValid
    //         opacity:       _root.opacity
    //         z:             QGroundControl.zOrderMapItems + 1

    //         sourceItem: MissionItemIndexLabel {
    //             checked:     true
    //             index:       _missionItem.sequenceNumber
    //             label:       ""
    //             onClicked:   _root.clicked(_missionItem.sequenceNumber)
    //         }
    //     }
    // }

    QGCDynamicObjectManager { id: objMgr }

    // Component.onCompleted: {
    //     objMgr.createObjects(
    //         [flightPathComponent, entryPointComponent],
    //         map,
    //         true /* parentObjectIsMap */)
    // }

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
