import QtQuick 2.6

Column {
    id: panel
    property var editor
    property var descriptor: editor.catalog.surface || {}
    property var reference: descriptor.rectangular_reference_height || {}
    property var local: descriptor.rectangular_local_support || {}
    property var patch: editor.surface.local_support_patch || {}
    property bool rectangular: editor.surface.type === "rectangular_bowl_v1"
    spacing: 7
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: "#8e9bb0"
        text: "Заданная геометрия опоры, не оценка глубины. Координаты — в системе автомобиля. Выключение удаляет optional поле из replacement."
    }
    ConfigCheck {
        objectName: "referenceHeightToggle"; text: "Задать высоту опоры"
        enabled: panel.rectangular && !!panel.descriptor.rectangular_reference_height
        checked: panel.editor.surface.reference_height_m !== undefined
        onClicked: panel.editor.setOptionalEnabled("reference_height_m", checked)
    }
    ConfigNumber {
        width: parent.width; title: "Высота опоры, м"
        visible: panel.editor.surface.reference_height_m !== undefined
        number: Number(panel.editor.surface.reference_height_m || 0)
        minimum: panel.reference.minimum !== undefined ? panel.reference.minimum : 0
        maximum: panel.reference.maximum !== undefined ? panel.reference.maximum : 5
        onEdited: function(value) { panel.editor.setField("surface", "reference_height_m", value) }
    }
    ConfigCheck {
        objectName: "localSupportToggle"; text: "Локальный приподнятый участок"
        enabled: panel.rectangular && !!panel.descriptor.rectangular_local_support
        checked: panel.editor.surface.local_support_patch !== undefined
        onClicked: panel.editor.setOptionalEnabled("local_support_patch", checked)
    }
    Column {
        width: parent.width; spacing: 7
        visible: panel.editor.surface.local_support_patch !== undefined
        Repeater {
            model: [
                {key: "center_x_m", title: "Центр X, м", center: true, x: true},
                {key: "center_y_m", title: "Центр Y, м", center: true, x: false},
                {key: "half_length_m", title: "Полудлина участка, м", x: true},
                {key: "half_width_m", title: "Полуширина участка, м", x: false},
                {key: "height_m", title: "Добавочная высота, м", height: true},
                {key: "transition_m", title: "Ширина плавного перехода, м", transition: true}
            ]
            ConfigNumber {
                width: parent.width; title: modelData.title
                property real extent: modelData.transition
                    ? Math.min(Number(panel.editor.surface.outer_half_length_m || 0), Number(panel.editor.surface.outer_half_width_m || 0))
                    : Number(panel.editor.surface[modelData.x ? "outer_half_length_m" : "outer_half_width_m"] || 0)
                number: Number(panel.patch[modelData.key] || 0)
                minimum: modelData.center ? -extent : modelData.height ? (panel.local.height_range_m || [0,5])[0]
                    : panel.local.minimum_extent_and_transition_m || 0.01
                maximum: modelData.height ? (panel.local.height_range_m || [0,5])[1] : extent
                onEdited: function(value) { panel.editor.setField("surface", "local_support_patch." + modelData.key, value) }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#ffca80"
            text: "Плато и переход должны полностью помещаться в носитель. Высота участка добавляется к опоре. Сервер проверит совместимость с виртуальной камерой."
        }
    }
}
