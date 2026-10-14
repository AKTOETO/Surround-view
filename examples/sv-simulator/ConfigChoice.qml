import QtQuick 2.6
import QtQuick.Controls 2.2

Column {
    id: field
    property string title
    property var options: []
    property string selectedValue
    signal picked(string value)
    spacing: 3
    Text { width: parent.width; text: field.title; color: "#cbd5e0"; wrapMode: Text.Wrap }
    ComboBox {
        width: parent.width
        model: field.options
        currentIndex: {
            for (var i = 0; i < field.options.length; ++i)
                if (field.options[i] === field.selectedValue) return i
            return -1
        }
        onActivated: field.picked(currentText)
    }
}
