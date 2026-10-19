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
    readonly property bool batch: bundleKind.currentIndex === 1
    readonly property bool receiptBatch: controller.bundleInfo.policy === "batch_bundle_v1"
    onBatchChanged: {
        directory = "";
        exportSource = "";
    }
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

    Label { text: "Тип переносимого набора" }
    ComboBox {
        id: bundleKind
        objectName: "researchBundleKind"
        width: parent.width
        model: ["Одиночный сценарий", "Серия одной серверной записи"]
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
                onClicked: editor.controller.verifyBundle(editor.directory, editor.batch)
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
        text: editor.batch ? "Экспортировать сохранённый batch-план" : "Экспортировать сохранённый сценарий"
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
                    placeholderText: editor.batch ? "Batch-план не выбран" : "Сохранённый сценарий не выбран"
                }
                Button {
                    text: editor.batch ? "Batch-план…" : "Сценарий…"
                    onClicked: files.item.open(false, editor.batch)
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
                onClicked: editor.controller.exportBundle(editor.exportSource, editor.exportParent, exportName.text, editor.batch)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: editor.batch ? "Экспортируется сохранённый план из 1–8 сценариев. Все references проверяются до создания папки. Используется одна запись сервера. Существующая папка не перезаписывается; при ошибке новая папка может остаться неполной." : "Экспортируется выбранный файл, а не текущая форма. Relative reference читается от папки сценария. Существующая папка не перезаписывается. При ошибке новая неполная папка может остаться."
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
                text: (editor.receiptBatch ? "Серия одной записи" : "Одиночный сценарий") + "\nПапка: " + (editor.controller.bundleInfo.directory || "") + (editor.receiptBatch ? "\nСценариев: " + (editor.controller.bundleInfo.runs || []).length + " · Trials: " + editor.controller.bundleInfo.trials : "\nФайлов: " + (editor.controller.bundleInfo.files || 0)) + "\nManifest SHA256: " + (editor.controller.bundleInfo.manifest_sha256 || "")
            }
            Label {
                objectName: "researchBundleMembers"
                width: parent.width
                visible: editor.receiptBatch
                wrapMode: Text.WrapAnywhere
                text: (editor.controller.bundleInfo.runs || []).map(function(run, index) {
                    return (index + 1) + ". " + run.id + " · trials: " + run.trials;
                }).join("\n")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Целостность набора не подтверждает независимость эталона или качество алгоритма. Эта квитанция относится к предыдущему чтению файлов."
            }
        }
    }
}
