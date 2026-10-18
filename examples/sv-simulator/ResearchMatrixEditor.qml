import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: editor
    objectName: "researchMatrixEditor"
    title: "Матрица параметров"
    property bool pyramidSelected: false
    property bool graphSelected: false
    property var capabilities: ({})
    property bool levelSweepEnabled: levelSweep.checked && pyramidSelected
    property bool solverSweepEnabled: solverSweep.checked && graphSelected
    property var axes: {
        var result = {};
        if (levelSweepEnabled)
            result.pyramid_levels_range = {"from": firstLevel.value, "to": lastLevel.value, "step": levelStep.value};
        if (solverSweepEnabled)
            result.seam_solvers = ["binary_pairs", "alpha_expansion"];
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
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Диапазон применяется только к multiband, решатели — только к graph-cut. Для остальных методов повторные варианты не создаются. Выбор сохраняется при смене методов/каталога; неподдерживаемую матрицу нужно исправить явно. Предел: 32 варианта, 10000 trials."
        }
    }
}
