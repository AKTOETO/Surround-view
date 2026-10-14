import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel
    property var editor
    spacing: 7
    function fieldTitle(key) {
        var titles = {
            flat_half_length_m: "Половина длины плоского участка, м",
            flat_half_width_m: "Половина ширины плоского участка, м",
            outer_half_length_m: "Внешняя полудлина, м", outer_half_width_m: "Внешняя полуширина, м",
            corner_height_m: "Высота края (0 — плоскость), м", dome_radius_m: "Радиус купола, м",
            dome_latitude_cells: "Сегменты купола по широте", dome_longitude_cells: "Сегменты по долготе",
            floor_radial_cells: "Радиальные сегменты пола", radius_m: "Радиус, м",
            height_m: "Высота, м", vertical_cells: "Вертикальные сегменты",
            angular_cells: "Угловые сегменты", half_extent_m: "Полуразмер куба, м", face_cells: "Сегменты грани"
        }
        return titles[key] || key
    }
    function geometry() {
        var surface = editor.catalog.surface
        return surface && surface.geometry_fields ? surface.geometry_fields[editor.surface.type] || {} : {}
    }
    ConfigChoice {
        width: parent.width; title: "Геометрия носителя"
        options: panel.editor.catalog.surface ? panel.editor.catalog.surface.types : []
        selectedValue: panel.editor.surface.type || ""
        onPicked: function(value) { panel.editor.setSurfaceType(value) }
    }
    Text {
        width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
        text: "Смена типа создаёт новый черновик и сбрасывает support/occlusion. Расширения опоры и AABB доступны только rectangular_bowl_v1."
    }
    TabBar {
        id: tabs; objectName: "surfaceTabs"; width: parent.width
        TabButton { text: "Геометрия" }
        TabButton { text: "Опора" }
        TabButton { text: "Видимость" }
    }
    StackLayout {
        width: parent.width; currentIndex: tabs.currentIndex
        height: currentIndex === 0 ? geometryForm.implicitHeight : currentIndex === 1 ? supportForm.implicitHeight : occlusionForm.implicitHeight
        Column {
            id: geometryForm; spacing: 7
            Repeater {
                model: panel.geometry().required || []
                ConfigNumber {
                    width: geometryForm.width; visible: modelData !== "uniform_cells"
                    height: visible ? implicitHeight : 0
                    title: panel.fieldTitle(modelData)
                    property var descriptor: panel.geometry()[modelData] || {}
                    number: Number(panel.editor.surface[modelData] || 0)
                    decimals: descriptor.type === "integer" ? 0 : 3
                    minimum: descriptor.minimum !== undefined ? descriptor.minimum : 0
                    maximum: descriptor.maximum !== undefined ? descriptor.maximum : 100
                    onEdited: function(value) { panel.editor.setField("surface", modelData, value) }
                }
            }
            Repeater {
                model: panel.editor.surface.uniform_cells ? 2 : 0
                ConfigNumber {
                    width: geometryForm.width; title: index === 0 ? "Сегменты сетки X" : "Сегменты сетки Y"
                    number: panel.editor.surface.uniform_cells[index]; decimals: 0
                    minimum: panel.geometry().uniform_cells ? panel.geometry().uniform_cells.items.minimum : 2
                    maximum: panel.geometry().uniform_cells ? panel.geometry().uniform_cells.items.maximum : 512
                    onEdited: function(value) { panel.editor.setField("surface", "uniform_cells." + index, value) }
                }
            }
        }
        SupportPanel { id: supportForm; editor: panel.editor }
        OcclusionPanel { id: occlusionForm; editor: panel.editor }
    }
}
