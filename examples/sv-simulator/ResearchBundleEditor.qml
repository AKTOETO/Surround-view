import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: editor

    property var controller
    property url directory: ""
    property url exportSource: ""
    property url exportParent: ""
    property bool choosingParent: false
    spacing: 10

    Loader {
        id: files
        visible: false
        source: simulatorQtMajorVersion >= 6 ? "ResearchFilesQt6.qml" : "ResearchFilesQt5.qml"
        onLoaded: {
            item.chosen.connect(function(file, reference) { editor.exportSource = file; });
            item.folderChosen.connect(function(folder) {
                if (editor.choosingParent)
                    editor.exportParent = folder;
                else
                    editor.directory = folder;
            });
        }
    }

    GroupBox {
        title: "Проверка и запуск набора"
        width: parent.width
        ColumnLayout {
            width: parent.width
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    placeholderText: "Папка с bundle.json не выбрана"
                    text: editor.directory.toString()
                }
                Button {
                    objectName: "researchBundleChoose"
                    text: "Выбрать папку…"
                    onClicked: {
                        editor.choosingParent = false;
                        files.item.chooseFolder();
                    }
                }
            }
            Button {
                objectName: "researchBundleVerify"
                text: "Проверить без сервера"
                enabled: editor.directory.toString().length > 0 && !editor.controller.busy
                onClicked: editor.controller.verifyBundle(editor.directory)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Проверяются manifest, SHA256, замкнутость ссылок и декодирование reference. Каждый запуск ниже повторяет проверку. Камерную запись на сервере нужно подготовить отдельно."
            }
        }
    }

    CheckBox {
        id: showExport
        objectName: "researchBundleShowExport"
        text: "Экспортировать сохранённый сценарий"
    }

    GroupBox {
        title: "Новая переносимая папка"
        visible: showExport.checked
        width: parent.width
        ColumnLayout {
            width: parent.width
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: editor.exportSource.toString()
                    placeholderText: "Сохранённый сценарий не выбран"
                }
                Button {
                    text: "Сценарий…"
                    onClicked: files.item.open(false, false)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: editor.exportParent.toString()
                    placeholderText: "Родительская папка не выбрана"
                }
                Button {
                    text: "Куда сохранить…"
                    onClicked: {
                        editor.choosingParent = true;
                        files.item.chooseFolder();
                    }
                }
            }
            Label { text: "Имя новой папки" }
            TextField {
                id: exportName
                objectName: "researchBundleExportName"
                Layout.fillWidth: true
                text: "research-bundle"
                selectByMouse: true
            }
            Button {
                objectName: "researchBundleExport"
                text: "Экспортировать без сервера"
                enabled: !editor.controller.busy && editor.exportSource.toString().length > 0 && editor.exportParent.toString().length > 0 && exportName.text.trim().length > 0
                onClicked: editor.controller.exportBundle(editor.exportSource, editor.exportParent, exportName.text)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Экспортируется выбранный файл, а не текущая форма. Relative reference читается от папки сценария. Существующая папка не перезаписывается. При ошибке новая неполная папка может остаться."
            }
        }
    }

    GroupBox {
        title: "Последняя операция с набором"
        visible: Object.keys(editor.controller.bundleInfo).length > 0
        width: parent.width
        Column {
            width: parent.width
            spacing: 6
            Label {
                objectName: "researchBundleReceipt"
                width: parent.width
                wrapMode: Text.WrapAnywhere
                text: "Папка: " + (editor.controller.bundleInfo.directory || "") + "\nФайлов: " + (editor.controller.bundleInfo.files || 0) + "\nManifest SHA256: " + (editor.controller.bundleInfo.manifest_sha256 || "")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Целостность набора не подтверждает независимость эталона или качество алгоритма. Эта квитанция относится к предыдущему чтению файлов."
            }
        }
    }
}
