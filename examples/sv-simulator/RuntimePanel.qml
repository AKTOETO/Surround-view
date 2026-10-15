import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel
    spacing: 8
    property var editor: configurationEditor
    property bool editable: editor.ready && !editor.busy && !editor.unresolved
    Text { text: "КОНФИГУРАЦИЯ СЕРВЕРА"; color: "#8e9bb0"; font.bold: true }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: "#64d2ff"
        text: "Сервер: " + editor.serverRevision + " · черновик: " + editor.baseRevision + "\n" + editor.status
    }
    Button { width: parent.width; text: "Обновить active / persisted"; enabled: !editor.busy; onClicked: editor.refresh() }
    Button { width: parent.width; text: "Загрузить active в формы"; enabled: panel.editable; onClicked: editor.loadActive() }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: "#cbd5e0"
        text: editor.hasRuntimeChanges ? "Есть временные отличия от сохранённого конфига" : "Active совпадает с persisted (после последнего чтения)"
    }
    TabBar {
        id: tabs; objectName: "configurationTabs"; width: parent.width
        TabButton { text: "Сшивка" }
        TabButton { text: "Носитель" }
        TabButton { text: "Операция" }
        TabButton { text: "Телеметрия" }
    }
    StackLayout {
        width: parent.width; currentIndex: tabs.currentIndex
        height: currentIndex === 0 ? fusionForm.implicitHeight : currentIndex === 1 ? surfaceForm.implicitHeight : currentIndex === 2 ? operationForm.implicitHeight : telemetryForm.implicitHeight
        Column {
            id: fusionForm; spacing: 7; enabled: panel.editable
            ConfigChoice {
                width: parent.width; title: "Алгоритм сшивки"
                options: panel.editor.catalog.fusion ? panel.editor.catalog.fusion.modes : []
                selectedValue: panel.editor.fusion.mode || ""
                onPicked: function(value) { panel.editor.setField("fusion", "mode", value) }
            }
            ConfigChoice {
                width: parent.width; title: "Диагностическое отображение"
                options: panel.editor.catalog.fusion ? panel.editor.catalog.fusion.diagnostics : []
                selectedValue: panel.editor.fusion.diagnostic || "color"
                onPicked: function(value) { panel.editor.setField("fusion", "diagnostic", value) }
            }
            Repeater {
                model: [
                    {key: "edge_width_px", title: "Ширина перехода, px", decimals: 2},
                    {key: "angle_power", title: "Степень углового веса", decimals: 3},
                    {key: "smoothness_weight", title: "Вес гладкости шва", decimals: 3},
                    {key: "pyramid_levels", title: "Число уровней пирамиды", decimals: 0}
                ]
                ConfigNumber {
                    width: fusionForm.width; title: modelData.title; decimals: modelData.decimals
                    property var bounds: panel.editor.catalog.fusion ? panel.editor.catalog.fusion[modelData.key] || {} : {}
                    number: Number(panel.editor.fusion[modelData.key] || 0)
                    minimum: bounds.min !== undefined ? bounds.min : bounds.exclusive_min !== undefined ? bounds.exclusive_min + Math.pow(10, -decimals) : 0
                    maximum: bounds.max !== undefined ? bounds.max : 100
                    onEdited: function(value) { panel.editor.setField("fusion", modelData.key, value) }
                }
            }
            ConfigChoice {
                width: parent.width; title: "Граница пирамиды"
                options: panel.editor.catalog.fusion ? panel.editor.catalog.fusion.pyramid_boundary : []
                selectedValue: panel.editor.fusion.pyramid_boundary || "zero"
                onPicked: function(value) { panel.editor.setField("fusion", "pyramid_boundary", value) }
            }
            ConfigChoice {
                width: parent.width; title: "Решатель шва"
                options: panel.editor.catalog.fusion ? panel.editor.catalog.fusion.seam_solver : []
                selectedValue: panel.editor.fusion.seam_solver || "binary_pairs"
                onPicked: function(value) { panel.editor.setField("fusion", "seam_solver", value) }
            }
            ConfigCheck {
                text: "Штриховка fallback"
                checked: panel.editor.fusion.fallback_display ? panel.editor.fusion.fallback_display.mode === "hatch" : false
                onClicked: panel.editor.setField("fusion", "fallback_display.mode", checked ? "hatch" : "legacy")
            }
            Text {
                width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
                text: "Fallback не является детектором неверной поверхности. CPU research-режимы ограничены серверным pixel budget."
            }
        }
        SurfacePanel { id: surfaceForm; editor: panel.editor; enabled: panel.editable }
        Column {
            id: operationForm; spacing: 7
            Text { width: parent.width; text: panel.editor.operationState; color: "#64d2ff"; wrapMode: Text.Wrap }
            Text { width: parent.width; text: panel.editor.operationId; color: "#8e9bb0"; wrapMode: Text.WrapAnywhere }
            Button { width: parent.width; text: "Проверить результат операции"; enabled: panel.editor.ready && !panel.editor.busy && panel.editor.operationId.length > 0; onClicked: panel.editor.checkOperation() }
            Button { width: parent.width; text: "Повторить тот же ID и запрос"; enabled: panel.editor.canRetry; onClicked: panel.editor.retryOperation() }
            Text {
                width: parent.width; color: "#cbd5e0"; wrapMode: Text.Wrap
                text: "После потери связи новые изменения блокируются до проверки. Журнал: 128 операций на процесс, без сохранения после restart. Черновик и ID хранятся только в этом GUI-процессе."
            }
            Repeater {
                model: panel.editor.comparison
                Text {
                    width: operationForm.width; color: "#cbd5e0"; wrapMode: Text.WrapAnywhere
                    text: modelData.field + "\nЗапрос: " + modelData.requested + "\nActive: " + modelData.active + "\nPersisted: " + modelData.persisted
                }
            }
            ConfigCheck { id: reviewed; visible: panel.editor.serverChanged; text: "Сверка после restart выполнена" }
            Button {
                width: parent.width; text: "Завершить сверку, загрузить active"
                visible: panel.editor.serverChanged
                enabled: reviewed.checked && panel.editor.ready && !panel.editor.busy
                onClicked: { panel.editor.acknowledgeUnknown(); reviewed.checked = false }
            }
        }
        Column {
            id: telemetryForm; spacing: 7
            enabled: panel.editable && !!panel.editor.catalog.telemetry
            ConfigCheck {
                objectName: "telemetryEnabled"
                text: "Собирать телеметрию на сервере"
                checked: panel.editor.telemetry.enabled || false
                onClicked: panel.editor.setField("telemetry", "enabled", checked)
            }
            ConfigNumber {
                objectName: "telemetryPeriod"; width: parent.width
                title: "Период sampling, мс"; decimals: 0
                number: panel.editor.telemetry.period_ms || 100
                minimum: panel.editor.catalog.telemetry ? panel.editor.catalog.telemetry.period_ms.min : 50
                maximum: panel.editor.catalog.telemetry ? panel.editor.catalog.telemetry.period_ms.max : 5000
                onEdited: function(value) { panel.editor.setField("telemetry", "period_ms", value) }
            }
            ConfigNumber {
                objectName: "telemetryCapacity"; width: parent.width
                title: "Server history, samples"; decimals: 0
                number: panel.editor.telemetry.history_capacity || 128
                minimum: panel.editor.catalog.telemetry ? panel.editor.catalog.telemetry.history_capacity.min : 8
                maximum: panel.editor.catalog.telemetry ? panel.editor.catalog.telemetry.history_capacity.max : 256
                onEdited: function(value) { panel.editor.setField("telemetry", "history_capacity", value) }
            }
            Text {
                width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
                text: "Изменение настроек сбрасывает серверную историю/epoch. Применить временно или сохранить — общие кнопки ниже. Остановка чтения графиков не выключает сбор."
            }
        }
    }
    Button { width: parent.width; text: editor.validated ? "Черновик проверен сервером" : "Проверить черновик"; enabled: panel.editable; onClicked: editor.validate() }
    Button { width: parent.width; text: "Применить временно"; enabled: editor.canApply; onClicked: editor.apply(false) }
    ConfigCheck { id: saveConfirmed; text: "Сохранить весь active snapshot"; enabled: panel.editable && editor.canPersist }
    Text {
        width: parent.width; color: "#ffca80"; wrapMode: Text.Wrap
        text: "Сохранение включает прежние временные настройки других групп. При experiment lease сервер отклонит новую операцию."
    }
    Button {
        width: parent.width; text: "Применить и сохранить на сервере"
        enabled: editor.canApply && editor.canPersist && saveConfirmed.checked
        onClicked: { editor.apply(true); saveConfirmed.checked = false }
    }
}
