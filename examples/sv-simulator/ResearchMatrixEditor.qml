import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: editor
    objectName: "researchMatrixEditor"
    title: "Матрица параметров"
    property bool pyramidSelected: false
    property bool graphSelected: false
    property var modes: []
    property var capabilities: ({})
    property bool levelSweepEnabled: levelSweep.checked && pyramidSelected
    property bool solverSweepEnabled: solverSweep.checked && graphSelected
    property var axes: {
        var result = {};
        if (levelSweepEnabled)
            result.pyramid_levels_range = {"from": firstLevel.value, "to": lastLevel.value, "step": levelStep.value};
        if (solverSweepEnabled)
            result.seam_solvers = ["binary_pairs", "alpha_expansion"];
        if (edge.included) result.edge_width_values = edge.values;
        if (angle.included) result.angle_power_values = angle.values;
        if (smoothness.included) result.smoothness_values = smoothness.values;
        return result;
    }
    Column {
        width: parent.width
        spacing: 8
        CheckBox {
            id: levelSweep
            objectName: "researchSweepLevels"
            text: "Сравнить диапазон уровней пирамиды"
            enabled: editor.pyramidSelected
        }
        GridLayout {
            visible: editor.levelSweepEnabled
            width: parent.width
            columns: width > 650 ? 6 : 2
            Label { text: "От" }
            SpinBox { id: firstLevel; objectName: "researchLevelFrom"; from: 1; to: 8; value: 2; editable: true }
            Label { text: "До (включительно)" }
            SpinBox { id: lastLevel; objectName: "researchLevelTo"; from: 1; to: 8; value: 4; editable: true }
            Label { text: "Шаг" }
            SpinBox { id: levelStep; objectName: "researchLevelStep"; from: 1; to: 8; value: 1; editable: true }
        }
        CheckBox {
            id: solverSweep
            objectName: "researchSweepSolvers"
            text: "Сравнить binary_pairs и alpha_expansion"
            enabled: editor.graphSelected && (!editor.capabilities.ready || checked ||
                     (editor.capabilities.solvers.indexOf("binary_pairs") !== -1 &&
                      editor.capabilities.solvers.indexOf("alpha_expansion") !== -1))
        }
        CheckBox {
            id: numericExpanded
            objectName: "researchShowNumericAxes"
            text: "Показать списки числовых параметров"
        }
        ResearchNumericAxis {
            id: edge
            visible: numericExpanded.checked
            objectName: "researchAxis_edge_width_px"
            width: parent.width
            parameter: "edge_width_px"
            caption: "Ширина смешивания, px"
            minimum: 0.001
            maximum: 4096
            initialValue: 24
            applicable: editor.modes.indexOf("edge_feather") !== -1 || editor.modes.indexOf("angular_feather") !== -1 || editor.modes.indexOf("multi_band") !== -1
        }
        ResearchNumericAxis {
            id: angle
            visible: numericExpanded.checked
            objectName: "researchAxis_angle_power"
            width: parent.width
            parameter: "angle_power"
            caption: "Степень углового веса"
            minimum: 0.001
            maximum: 32
            initialValue: 2
            applicable: editor.modes.indexOf("angular_feather") !== -1
        }
        ResearchNumericAxis {
            id: smoothness
            visible: numericExpanded.checked
            objectName: "researchAxis_smoothness_weight"
            width: parent.width
            parameter: "smoothness_weight"
            caption: "Регуляризация graph-cut"
            minimum: 0
            maximum: 100
            initialValue: 0.1
            applicable: editor.graphSelected
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Без явных числовых списков используются defaults сценария: ширина 24 px, степень 2, smoothness 0.1 (не active сервера). Диапазон применяется только к multiband, решатели — только к graph-cut. Для остальных методов повторные варианты не создаются. Выбор сохраняется при смене методов/каталога; неподдерживаемую матрицу нужно исправить явно. Предел: 32 варианта, 10000 trials."
        }
    }
}
