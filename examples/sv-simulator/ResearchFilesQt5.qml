import QtQuick 2.6
import QtQuick.Dialogs 1.3

Item {
    id: dialogs

    property bool reference: false

    signal chosen(url file, bool reference)

    function open(forReference) {
        reference = forReference;
        chooser.open();
    }

    FileDialog {
        id: chooser

        title: dialogs.reference ? "Выбрать внешний reference manifest" : "Выбрать сохранённый сценарий"
        selectExisting: true
        nameFilters: ["JSON (*.json)"]
        onAccepted: dialogs.chosen(fileUrl, dialogs.reference)
    }

}
