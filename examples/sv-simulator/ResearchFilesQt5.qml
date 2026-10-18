import QtQuick 2.6
import QtQuick.Dialogs 1.3

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

        title: dialogs.reference ? "Выбрать внешний reference manifest" : dialogs.batch ? "Выбрать batch-план" : "Выбрать сохранённый сценарий"
        selectExisting: true
        nameFilters: ["JSON (*.json)"]
        onAccepted: dialogs.chosen(fileUrl, dialogs.reference)
    }

    FileDialog {
        id: saveDialog

        objectName: "researchSaveDialog"
        title: "Сохранить черновик в новый файл"
        selectExisting: false
        defaultSuffix: "json"
        nameFilters: ["Batch plan (*.json)"]
        onAccepted: dialogs.saveChosen(fileUrl)
    }

}
