import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel

    property var controller
    property bool batch: false
    property bool ready: false
    property string issue: ""
    property url parentDirectory: ""
    signal exportRequested(url parentDirectory, string name)
    signal useBundle(url directory, bool batch)
    spacing: 8

    Loader {
        id: files
        visible: false
        source: simulatorQtMajorVersion >= 6 ? "ResearchFilesQt6.qml" : "ResearchFilesQt5.qml"
        onLoaded: item.folderChosen.connect(function(folder) { panel.parentDirectory = folder; })
    }

    CheckBox {
        id: expanded
        objectName: "researchDraftBundleExpand"
        text: panel.batch ? "Упаковать текущий состав серии" : "Упаковать текущую форму"
    }

    GroupBox {
        title: "Экспорт без сохранения исходного JSON"
        width: parent.width
        visible: expanded.checked
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: panel.batch ? "Сохраняется текущий порядок и ID участников без файла плана. Сами сценарии и их references читаются из выбранных файлов. Одна серверная запись, до 8 участников." : "Сохраняются текущие параметры, матрицы и критерии. Reference проверяется и копируется. Подключение не требуется; совместимость с сервером проверится при запуске."
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: panel.parentDirectory.toString()
                    placeholderText: "Родительская папка не выбрана"
                }
                Button {
                    text: "Куда сохранить…"
                    onClicked: files.item.chooseFolder()
                }
            }
            Label { text: "Имя новой папки" }
            TextField {
                id: name
                objectName: "researchDraftBundleName"
                Layout.fillWidth: true
                selectByMouse: true
                text: panel.batch ? "series-bundle" : "form-bundle"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: !panel.ready && panel.issue.length > 0
                text: panel.issue
            }
            Button {
                objectName: "researchDraftBundleStart"
                text: "Экспортировать текущий план"
                enabled: panel.ready && !panel.controller.busy && panel.parentDirectory.toString().length > 0 && name.text.trim().length > 0
                onClicked: panel.exportRequested(panel.parentDirectory, name.text)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                visible: !!panel.controller.bundleInfo.directoryUrl
                text: "Последний проверенный набор: " + (panel.controller.bundleInfo.directory || "") + "\nИзменение формы не меняет ранее созданный набор. При новом запуске содержимое проверится заново."
            }
            Button {
                objectName: "researchDraftBundleUse"
                text: "Открыть последний набор"
                enabled: !panel.controller.busy && !!panel.controller.bundleInfo.directoryUrl
                onClicked: panel.useBundle(panel.controller.bundleInfo.directoryUrl, panel.controller.bundleInfo.policy === "batch_bundle_v1")
            }
        }
    }
}
