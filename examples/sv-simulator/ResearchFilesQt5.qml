import QtQuick 2.6
import QtQuick.Dialogs 1.3

Item {
    id: dialogs

    property bool reference: false
    property bool batch: false

    signal chosen(url file, bool reference)

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

}
