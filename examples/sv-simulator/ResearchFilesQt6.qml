import QtQuick
import QtQuick.Dialogs

Item {
    id: dialogs

    property bool reference: false
    property bool batch: false

    signal chosen(url file, bool reference)
    signal saveChosen(url file)

    function save() {
        saveDialog.open();
    }

    function open(forReference, forBatch) {
        batch = !!forBatch;
        reference = forReference;
        chooser.open();
    }

    FileDialog {
        id: chooser

        objectName: "researchFileDialog"
        title: dialogs.reference ? "Выбрать внешний reference manifest" : dialogs.batch ? "Выбрать batch-план" : "Выбрать сохранённый сценарий"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON (*.json)"]
        options: FileDialog.DontUseNativeDialog
        onAccepted: dialogs.chosen(selectedFile, dialogs.reference)
    }

    FileDialog {
        id: saveDialog

        objectName: "researchSaveDialog"
        title: "Сохранить черновик в новый файл"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Batch plan (*.json)"]
        options: FileDialog.DontUseNativeDialog | FileDialog.DontConfirmOverwrite
        onAccepted: dialogs.saveChosen(selectedFile)
    }

}
