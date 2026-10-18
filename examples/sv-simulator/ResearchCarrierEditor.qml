import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: editor
    title: "Набор носителей: снимки active"
    property var activeSurface: ({})
    property bool canCapture: false
    property var profiles: []
    property string notice: ""
    property var axes: useProfiles.checked ? ({"surface_profiles": profiles}) : ({})

    function addActive() {
        if (!canCapture || !activeSurface.type) return;
        var copy = JSON.parse(JSON.stringify(activeSurface));
        for (var i = 0; i < profiles.length; ++i) {
            if (sameValue(profiles[i], copy)) {
                notice = "Этот полный профиль уже добавлен";
                return;
            }
        }
        if (profiles.length >= 32) {
            notice = "Допускается не более 32 профилей";
            return;
        }
        profiles = profiles.concat([copy]);
        notice = "Добавлен снимок active; черновик конфигуратора не используется";
    }

    function sameValue(a, b) {
        if (a === b) return true;
        if (a === null || b === null || typeof a !== "object" || typeof b !== "object") return false;
        var keys = Object.keys(a);
        if (keys.length !== Object.keys(b).length) return false;
        for (var i = 0; i < keys.length; ++i) {
            var key = keys[i];
            if (!Object.prototype.hasOwnProperty.call(b, key) || !sameValue(a[key], b[key])) return false;
        }
        return true;
    }

    function summary(surface) {
        var parts = [surface.type];
        var fields = ["dome_radius_m", "radius_m", "half_extent_m", "height_m", "flat_half_length_m", "flat_half_width_m", "outer_half_length_m", "outer_half_width_m", "corner_height_m"];
        for (var i = 0; i < fields.length; ++i) {
            if (surface[fields[i]] !== undefined) parts.push(fields[i] + "=" + surface[fields[i]]);
        }
        return parts.join(" · ");
    }

    ColumnLayout {
        width: parent.width
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Настройте и примените носитель во вкладке геометрии, обновите active, затем добавьте его сюда. Повторите для других размеров или типов. Снимки сохраняются целиком, включая support/occlusion."
        }
        CheckBox {
            id: useProfiles
            objectName: "researchUseCarriers"
            text: "Сравнивать набор носителей × варианты сшивки"
        }
        Button {
            objectName: "researchCaptureCarrier"
            text: "Добавить текущий active: " + (editor.activeSurface.type || "нет снимка")
            enabled: editor.canCapture && !!editor.activeSurface.type
            onClicked: editor.addActive()
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: editor.notice || (useProfiles.checked ? "Профилей: " + editor.profiles.length : "Используется исходный носитель сервера; сохранённый набор выключен")
        }
        Repeater {
            model: editor.profiles
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "#" + index + " · " + editor.summary(modelData)
                }
                Button {
                    text: "Удалить"
                    onClicked: {
                        var values = editor.profiles.slice(0);
                        values.splice(index, 1);
                        editor.profiles = values;
                        editor.notice = "";
                    }
                }
            }
        }
    }
}
