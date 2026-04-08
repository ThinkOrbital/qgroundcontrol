// Custom builds can override this file to add custom guided actions.

import QtQml

QtObject {
    // Your new actions
    /*
    readonly property int actionStartScanMission:   _guidedController.customActionStart + 0
    readonly property int actionEndScanMission:     _guidedController.customActionStart + 1
    readonly property int actionResumeScanMission:  _guidedController.customActionStart + 2

    readonly property string startScanMissionTitle:    qsTr("Start Mission")
    readonly property string endScanMissionTitle:      qsTr("End Mission")
    readonly property string resumeScanMissionTitle:   qsTr("Resume Mission")
    */

    function customConfirmAction(actionCode, actionData, mapIndicator, confirmDialog) {
        switch (actionCode) {
        /*
        case actionStartScanMission:
            confirmDialog.hideTrigger = true
            confirmDialog.title = startScanMissionTitle
            confirmDialog.message = qsTr("Start the scan mission?")
            break
        case actionEndScanMission:
            confirmDialog.hideTrigger = true
            confirmDialog.title =   endScanMissionTitle
            confirmDialog.message = qsTr("End the scan mission?")
            break
        case actionResumeScanMission:
            confirmDialog.hideTrigger = true
            confirmDialog.title =   resumeScanMissionTitle
            confirmDialog.message = qsTr("Resume the scan mission?")
            break
        */
        default:
            return false
        }

        return true // true = action handled here
    }

    function customExecuteAction(actionCode, actionData, sliderOutputValue, optionCheckedode) {
        switch (actionCode) {
        /*
        case actionStartScanMission:
            backend.startMission()
            break
        case actionEndScanMission:
            backend.endMission()
            break
        case actionResumeScanMission:
            backend.resumeMission()
            break
        */
        default:
            return false // false = action not handled here
        }

        return true // true = action handled here
    }
}
