import QtQuick 2.6
import QtQuick.Controls 2.2

Column {
    id: panel
    property var model
    property var choices: []
    property string choiceName
    property bool quantiles: true
    property var selected: choices.length && choice.currentIndex >= 0 ? choices[choice.currentIndex] : ({key: "", label: "—", unit: "", note: ""})
    spacing: 7
    ComboBox {
        id: choice; objectName: panel.choiceName; width: parent.width
        model: panel.choices; textRole: "label"
    }
    Text { width: parent.width; text: panel.selected.note; color: "#8e9bb0"; wrapMode: Text.Wrap }
    TelemetryChart {
        objectName: panel.choiceName + "Chart"
        width: parent.width; points: panel.model.points
        metric: panel.selected.key; title: panel.selected.label + ", " + panel.selected.unit
    }
    Text {
        visible: panel.quantiles
        width: parent.width; color: "#cbd5e0"; wrapMode: Text.Wrap
        property var rows: panel.model.points
        text: rows.length >= 0 ? panel.model.metricSummary(panel.selected.key) : ""
    }
}
