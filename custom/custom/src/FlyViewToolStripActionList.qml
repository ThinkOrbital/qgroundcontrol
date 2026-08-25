import QtQml.Models

import QGroundControl
import QGroundControl.Controls

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist

    model: [
        PreFlightCheckListShowAction { onTriggered: displayPreFlightChecklist() },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionRTL { },
        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },

        ToolStripAction {
            text:       backend.nudgeMode === 0 ? qsTr("Nudge\nAbs") : qsTr("Nudge\nRel")
            iconSource: "/res/gear-white.svg"
            visible:    true
            enabled:    true
            onTriggered: backend.toggleNudgeMode()
        }
    ]
}
