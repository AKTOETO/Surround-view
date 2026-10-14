import QtQuick 2.6
import QtQuick.Controls 2.2

Column {
    id: field
    property string title
    property real number: 0
    property real minimum: 0
    property real maximum: 100
    property int decimals: 3
    property real step: decimals === 0 ? 1 : 0.1
    property real factor: Math.pow(10, decimals)
    signal edited(real value)
    spacing: 3
    Text { width: parent.width; text: field.title; color: "#cbd5e0"; wrapMode: Text.Wrap }
    SpinBox {
        id: spin
        width: parent.width
        from: Math.round(field.minimum * field.factor)
        to: Math.round(Math.max(field.maximum, field.number) * field.factor)
        value: Math.round(field.number * field.factor)
        stepSize: Math.max(1, Math.round(field.step * field.factor))
        editable: true
        validator: DoubleValidator {
            bottom: spin.from / field.factor
            top: spin.to / field.factor
            decimals: field.decimals
            notation: DoubleValidator.StandardNotation
        }
        textFromValue: function(value, locale) {
            return Number(value / field.factor).toLocaleString(locale, 'f', field.decimals)
        }
        valueFromText: function(text, locale) {
            return Math.round(Number.fromLocaleString(locale, text) * field.factor)
        }
        onValueModified: field.edited(value / field.factor)
    }
}
