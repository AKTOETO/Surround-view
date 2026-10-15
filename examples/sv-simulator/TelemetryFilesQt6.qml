import QtQuick
import QtQuick.Dialogs

Item {
    id: dialogs
    signal chosen(url file, bool saving)
    function save() { saveDialog.open() }
    function open() { openDialog.open() }
    FileDialog {
        id: saveDialog; objectName: "telemetrySaveDialog"
        title: "Сохранить окно телеметрии"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Telemetry window (*.json)"]
        options: FileDialog.DontUseNativeDialog
        onAccepted: dialogs.chosen(selectedFile, true)
    }
    FileDialog {
        id: openDialog; objectName: "telemetryOpenDialog"
        title: "Открыть окно телеметрии"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Telemetry window (*.json)"]
        options: FileDialog.DontUseNativeDialog
        onAccepted: dialogs.chosen(selectedFile, false)
    }
}
