import QtQuick 2.6

Column {
    id: panel
    property var editor
    property var descriptor: editor.catalog.surface ? editor.catalog.surface.rectangular_source_occlusion || {} : {}
    property var box: editor.surface.source_occlusion_box || {}
    property var uncertainty: box.uncertainty || {}
    property string mode: box.mode || descriptor.default_mode || "center"
    property bool rectangular: editor.surface.type === "rectangular_bowl_v1"
    spacing: 7
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: "#8e9bb0"
        text: "Один заданный непрозрачный AABB в системе автомобиля. Его координаты не извлекаются из сцены автоматически; это не depth map."
    }
    ConfigCheck {
        objectName: "sourceOcclusionToggle"; text: "Проверять заданный AABB"
        enabled: panel.rectangular && !!panel.editor.catalog.surface && !!panel.editor.catalog.surface.rectangular_source_occlusion
        checked: panel.editor.surface.source_occlusion_box !== undefined
        onClicked: panel.editor.setOptionalEnabled("source_occlusion_box", checked)
    }
    Column {
        width: parent.width; spacing: 7
        visible: panel.editor.surface.source_occlusion_box !== undefined
        Repeater {
            model: ["min_m", "max_m"]
            Column {
                id: bound; width: parent.width; spacing: 7
                property string boundKey: modelData
                Repeater {
                    model: ["X", "Y", "Z"]
                    ConfigNumber {
                        width: bound.width
                        title: (bound.boundKey === "min_m" ? "Минимум " : "Максимум ") + modelData + ", м"
                        number: panel.box[bound.boundKey] ? panel.box[bound.boundKey][index] : 0
                        minimum: (panel.descriptor.coordinate_range_m || [-100,100])[0]
                        maximum: (panel.descriptor.coordinate_range_m || [-100,100])[1]
                        onEdited: function(value) { panel.editor.setField("surface", "source_occlusion_box." + bound.boundKey + "." + index, value) }
                    }
                }
            }
        }
        ConfigNumber {
            width: parent.width; title: "Допуск пересечения, м"; decimals: 6; step: 0.001
            number: Number(panel.box.tolerance_m || 0)
            minimum: (panel.descriptor.tolerance_range_m || [0,0.1])[0]
            maximum: (panel.descriptor.tolerance_range_m || [0,0.1])[1]
            onEdited: function(value) { panel.editor.setField("surface", "source_occlusion_box.tolerance_m", value) }
        }
        ConfigChoice {
            objectName: "occlusionModeChoice"; width: parent.width; title: "Политика видимости"
            options: panel.descriptor.modes || []
            selectedValue: panel.mode
            onPicked: function(value) { panel.editor.setField("surface", "source_occlusion_box.mode", value) }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#8e9bb0"
            text: "center — центральный луч; radial — радиальное приближение footprint; box-surface — footprint на грани AABB. Смена режима сбрасывает несовместимые поля. Footprint требует камер без distortion."
        }
        ConfigNumber {
            objectName: "guardTexelsField"; width: parent.width; title: "Защитные texels"; decimals: 0
            enabled: panel.mode !== "center"
            number: Number(panel.box.guard_texels || 0)
            minimum: (panel.descriptor.guard_texels_range || [0,1])[0]
            maximum: (panel.descriptor.guard_texels_range || [0,1])[1]
            onEdited: function(value) { panel.editor.setField("surface", "source_occlusion_box.guard_texels", value) }
        }
        ConfigNumber {
            objectName: "supportToleranceField"; width: parent.width; title: "Допуск принадлежности грани, м"; decimals: 6; step: 0.00001
            visible: panel.mode === "bilinear_box_surface_v1"
            number: panel.box.support_tolerance_m !== undefined ? panel.box.support_tolerance_m : panel.descriptor.default_support_tolerance_m || 0.00001
            minimum: (panel.descriptor.support_tolerance_range_m || [0,0.05])[0]
            maximum: (panel.descriptor.support_tolerance_range_m || [0,0.05])[1]
            onEdited: function(value) { panel.editor.setField("surface", "source_occlusion_box.support_tolerance_m", value) }
        }
        ConfigCheck {
            text: "Требовать обращённую к камере грань"
            visible: panel.mode === "bilinear_box_surface_v1"
            checked: !!panel.box.require_facing_surface
            onClicked: panel.editor.setField("surface", "source_occlusion_box.require_facing_surface", checked)
        }
        ConfigCheck {
            objectName: "sourceUncertaintyToggle"; text: "Учитывать неопределённость луча"
            enabled: !!panel.descriptor.uncertainty
            checked: panel.box.uncertainty !== undefined
            onClicked: panel.editor.setOptionalEnabled("uncertainty", checked)
        }
        Column {
            width: parent.width; spacing: 7
            visible: panel.box.uncertainty !== undefined
            ConfigNumber {
                width: parent.width; title: "Радиус неопределённости, м"; decimals: 6; step: 0.001
                number: Number(panel.uncertainty.radius_m || 0)
                minimum: panel.descriptor.uncertainty ? panel.descriptor.uncertainty.radius_range_m[0] : 0
                maximum: panel.descriptor.uncertainty ? panel.descriptor.uncertainty.radius_range_m[1] : 0.05
                onEdited: function(value) { panel.editor.setField("surface", "source_occlusion_box.uncertainty.radius_m", value) }
            }
            ConfigChoice {
                width: parent.width; title: "Неоднозначный луч"
                options: panel.descriptor.uncertainty ? panel.descriptor.uncertainty.policies : []
                selectedValue: panel.uncertainty.ambiguous_policy || "accept"
                onPicked: function(value) { panel.editor.setField("surface", "source_occlusion_box.uncertainty.ambiguous_policy", value) }
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; color: "#8e9bb0"
                text: "Неопределённость относится к центральным лучам; footprint не расширяется. accept/reject — заданная политика, не оценка достоверности сцены."
            }
        }
    }
}
