import QtQuick 2.6
import QtQuick.Dialogs 1.3

Item {
    id: dialogs
    signal chosen(url file, bool saving)
    function save() { saveDialog.open() }
    function open() { openDialog.open() }
    FileDialog {
        id: saveDialog
        title: "Сохранить окно телеметрии"
        selectExisting: false
        defaultSuffix: "json"
        nameFilters: ["Telemetry window (*.json)"]
        onAccepted: dialogs.chosen(fileUrl, true)
    }
    FileDialog {
        id: openDialog
        title: "Открыть окно телеметрии"
        selectExisting: true
        nameFilters: ["Telemetry window (*.json)"]
        onAccepted: dialogs.chosen(fileUrl, false)
    }
}
