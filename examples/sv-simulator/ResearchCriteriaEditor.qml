import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: editor
    objectName: "researchCriteriaEditor"
    title: "Критерии качества опыта"
    property bool overrideEnabled: overrideChoice.checked
    property real maeLimit: 0
    property real rmseLimit: 0
    property real psnrLimit: 0
    property var criteria: {
        var result = {};
        if (useMae.checked) result.max_mae_rgb8 = maeLimit;
        if (useRmse.checked) result.max_rmse_rgb8 = rmseLimit;
        if (usePsnr.checked) result.min_psnr_db = psnrLimit;
        return result;
    }
    Column {
        width: parent.width
        spacing: 8
        CheckBox {
            id: overrideChoice
            objectName: "researchOverrideCriteria"
            text: "Задать полный набор критериев для этого опыта"
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: editor.overrideEnabled
                ? "Выбранные пределы полностью заменяют criteria эталона. Отметьте хотя бы один. Они фиксируются до запуска; ноль — допустимый явный порог, не рекомендованное значение. Нужен внешний reference."
                : "Наследовать criteria из reference manifest, если они там заданы. Исходный manifest не изменяется."
        }
        GridLayout {
            visible: editor.overrideEnabled
            width: parent.width
            columns: width > 650 ? 2 : 1
            CheckBox { id: useMae; objectName: "researchUseMae"; text: "Ограничить MAE" }
            ConfigNumber {
                objectName: "researchMaeLimit"
                Layout.fillWidth: true
                enabled: useMae.checked
                title: "MAE RGB8 ≤, 0…255"
                maximum: 255; decimals: 6; step: 0.001
                number: editor.maeLimit
                onEdited: function(value) { editor.maeLimit = value; }
            }
            CheckBox { id: useRmse; objectName: "researchUseRmse"; text: "Ограничить RMSE" }
            ConfigNumber {
                objectName: "researchRmseLimit"
                Layout.fillWidth: true
                enabled: useRmse.checked
                title: "RMSE RGB8 ≤, 0…255"
                maximum: 255; decimals: 6; step: 0.001
                number: editor.rmseLimit
                onEdited: function(value) { editor.rmseLimit = value; }
            }
            CheckBox { id: usePsnr; objectName: "researchUsePsnr"; text: "Ограничить PSNR" }
            ConfigNumber {
                objectName: "researchPsnrLimit"
                Layout.fillWidth: true
                enabled: usePsnr.checked
                title: "PSNR ≥, dB, 0…200"
                maximum: 200; decimals: 6; step: 0.001
                number: editor.psnrLimit
                onEdited: function(value) { editor.psnrLimit = value; }
            }
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            visible: editor.overrideEnabled
            text: "Приёмка требует выполнения всех выбранных пределов на каждом measured sample; warmup исключён. Полнота исполнения и restore обязательны. Точность формы: 6 знаков после запятой. Пороги и независимость truth требуют отдельного обоснования."
        }
    }
}
