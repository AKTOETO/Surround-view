import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: axis
    property string parameter
    property string caption
    property real minimum: 0
    property real maximum: 100
    property real initialValue: 1
    property bool applicable: false
    property var values: [initialValue]
    property bool included: includeAxis.checked && applicable
    property string notice: ""
    title: caption

    function addValue() {
        var value = number.number;
        if (values.indexOf(value) !== -1) {
            notice = "Это значение уже добавлено";
            return;
        }
        if (values.length >= 8) return;
        values = values.concat([value]);
        notice = "";
    }

    ColumnLayout {
        width: parent.width
        CheckBox {
            id: includeAxis
            objectName: "researchUse_" + axis.parameter
            text: "Включить явный список значений"
            enabled: axis.applicable
        }
        ColumnLayout {
            visible: axis.included
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                ConfigNumber {
                    id: number
                    objectName: "researchValue_" + axis.parameter
                    Layout.fillWidth: true
                    title: axis.caption
                    minimum: axis.minimum
                    maximum: axis.maximum
                    number: axis.initialValue
                    decimals: 3
                    onEdited: function(value) { number.number = value; }
                }
                Button {
                    objectName: "researchAdd_" + axis.parameter
                    text: "Добавить"
                    enabled: axis.values.length < 8
                    onClicked: axis.addValue()
                }
            }
            Repeater {
                model: axis.values
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Number(modelData).toLocaleString(Qt.locale(), 'g', 12); Layout.fillWidth: true }
                    Button {
                        objectName: "researchRemove_" + axis.parameter + "_" + index
                        text: "Удалить"
                        onClicked: {
                            var next = axis.values.slice(0);
                            next.splice(index, 1);
                            axis.values = next;
                        }
                    }
                }
            }
            Label { text: axis.notice; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }
}
