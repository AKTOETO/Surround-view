import QtQuick 2.6
import QtQuick.Controls 2.2

CheckBox {
    id: control
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? "#cbd5e0" : "#718096"
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
