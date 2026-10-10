import QtQuick 2.6

Column {
    id: panel
    spacing: 8
    Text { text: "АЛГОРИТМЫ И ГЕОМЕТРИЯ СЕРВЕРА"; color: "#8e9bb0"; font.bold: true }
    Text {
        width: parent.width
        text: "Ревизия: " + runtimeSettings.revision + "\n" + runtimeSettings.status
        color: "#64d2ff"; wrapMode: Text.Wrap
    }
    Text {
        width: parent.width
        text: "Изменения временные. Загрузите снимок, измените JSON и примените. Сервер проверяет параметры и ревизию; файл конфигурации не изменяется."
        color: "#cbd5e0"; wrapMode: Text.Wrap; font.pixelSize: 11
    }
    Rectangle {
        width: parent.width; height: 30; color: "#222a3a"; radius: 4
        Text { anchors.centerIn: parent; text: "Обновить состояние и catalog"; color: "white" }
        MouseArea { anchors.fill: parent; onClicked: runtimeSettings.refresh() }
    }
    Repeater {
        model: ["fusion", "surface"]
        Column {
            id: editor
            width: panel.width; spacing: 6
            property string baseRevision: ""
            Connections {
                target: runtimeSettings
                onChanged: {
                    if (!runtimeSettings.ready) {
                        editor.baseRevision = ""
                        draft.text = ""
                    }
                }
            }
            Text { text: modelData + " · снимок " + editor.baseRevision; color: "#e2e8f0" }
            Rectangle {
                width: parent.width; height: 30; color: "#222a3a"; radius: 4
                opacity: runtimeSettings.ready ? 1 : 0.4
                Text { anchors.centerIn: parent; text: "Загрузить текущий снимок"; color: "white" }
                MouseArea {
                    anchors.fill: parent; enabled: runtimeSettings.ready && !runtimeSettings.pending
                    onClicked: {
                        draft.text = modelData === "fusion" ? runtimeSettings.fusionJson : runtimeSettings.surfaceJson
                        editor.baseRevision = runtimeSettings.revision
                    }
                }
            }
            Rectangle {
                width: parent.width; height: 210; color: "#111620"; border.color: "#384358"
                Flickable {
                    anchors.fill: parent; anchors.margins: 6; clip: true
                    contentWidth: width; contentHeight: draft.contentHeight
                    TextEdit {
                        id: draft; width: parent.width; color: "#e2e8f0"
                        font.family: "monospace"; font.pixelSize: 11
                        wrapMode: TextEdit.Wrap; selectByMouse: true
                    }
                }
            }
            Rectangle {
                width: parent.width; height: 30; color: "#155451"; radius: 4
                opacity: runtimeSettings.ready && !runtimeSettings.pending && editor.baseRevision.length ? 1 : 0.4
                Text { anchors.centerIn: parent; text: "Применить " + modelData; color: "white" }
                MouseArea {
                    anchors.fill: parent
                    enabled: runtimeSettings.ready && !runtimeSettings.pending && editor.baseRevision.length > 0
                    onClicked: {
                        if (modelData === "fusion") runtimeSettings.applyFusion(draft.text, editor.baseRevision)
                        else runtimeSettings.applySurface(draft.text, editor.baseRevision)
                    }
                }
            }
        }
    }
    Text { text: "Возможности и пределы сервера"; color: "#e2e8f0" }
    TextEdit {
        width: parent.width; text: runtimeSettings.catalogJson; readOnly: true
        selectByMouse: true; wrapMode: TextEdit.Wrap; color: "#8e9bb0"
        font.family: "monospace"; font.pixelSize: 10
    }
}
