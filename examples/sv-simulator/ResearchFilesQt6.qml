import QtQuick
import QtQuick.Dialogs

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

        objectName: "researchFileDialog"
        title: dialogs.reference ? "Выбрать внешний reference manifest" : "Выбрать сохранённый сценарий"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON (*.json)"]
        options: FileDialog.DontUseNativeDialog
        onAccepted: dialogs.chosen(selectedFile, dialogs.reference)
    }

}
