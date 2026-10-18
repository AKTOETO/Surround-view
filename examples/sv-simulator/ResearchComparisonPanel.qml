import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel

    property var controller: null
    property bool researchBusy: false
    property url leftFile: ""
    property url rightFile: ""
    property bool selectingLeft: true
    property var comparisonData: controller ? controller.result : ({
    })

    function metric(value) {
        if (value === undefined || value === null)
            return "—";

        if (value !== 0 && Math.abs(value) < 0.001)
            return Number(value).toPrecision(6);

        return Number(value).toFixed(3);
    }

    function summary(value) {
        if (!value)
            return "Нет сопоставленного варианта";

        return "Render p50/p95: " + metric(value.render_p50_ms) + " / " + metric(value.render_p95_ms) + " мс · n=" + value.measured + "\nMAE/RMSE: " + metric(value.mae) + " / " + metric(value.rmse) + " · PSNR: " + (value.exact_match ? "∞ (точно)" : metric(value.psnr)) + " dB · frames=" + value.quality_frames;
    }

    function platform(value) {
        if (!value)
            return "Паспорт ещё не загружен";

        return "workflow=" + value.success + " · restore=" + value.restored + " · quality=" + value.quality_status + " · criteria=" + (value.criteria_source || "unverified") + " [" + (value.criteria ? fields(value.criteria, "").join(", ") : "не заданы") + "] · truth=" + (value.reference_kind || "неизвестно") + " · dataset=" + (value.reference_dataset_id || "неизвестно") + "\n" + (value.backend || "backend неизвестен") + " · " + (value.gl_vendor || "vendor неизвестен") + " · " + (value.gl_renderer || "renderer неизвестен") + "\nsource: " + (value.source_revision || "неизвестно") + " · fingerprint: " + (value.source_fingerprint || "неизвестно") + "\n" + observations(value.observation_policy);
    }

    function observations(value) {
        if (!value) return "Нагрузка наблюдения не подтверждена";
        return "capture=" + value.capture_frames + " (" + value.capture_consumer + ") · evaluator=" + value.quality_evaluation + " · progress=" + value.progress_callback + " · telemetry=" + value.telemetry_before.enabled + " / " + value.telemetry_before.period_ms + " ms / " + value.telemetry_before.history_capacity;
    }

    function fields(object, prefix) {
        var text = [];
        for (var key in object) {
            var value = object[key];
            var name = prefix ? prefix + "." + key : key;
            if (value !== null && typeof value === "object" && !Array.isArray(value))
                text = text.concat(fields(value, name));
            else
                text.push(name + " = " + String(value));
        }
        return text;
    }

    spacing: 10
    objectName: "researchComparisonPanel"

    Loader {
        id: files

        objectName: "comparisonFilesLoader"
        visible: false
        source: simulatorQtMajorVersion >= 6 ? "ResearchFilesQt6.qml" : "ResearchFilesQt5.qml"
        onLoaded: {
            item.openTitle = "Выбрать individual report.json или ID.json";
            item.saveTitle = "Сохранить сравнение в новый файл";
            item.chosen.connect(function(file, reference) {
                if (panel.selectingLeft)
                    panel.leftFile = file;
                else
                    panel.rightFile = file;
            });
            item.saveChosen.connect(function(file) {
                if (panel.controller)
                    panel.controller.save(file);

            });
        }
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: "Два individual ExperimentRunner отчёта, до 16 MiB каждый. Полные настройки сопоставляются; batch.json не подходит. Файлы/хэши — заявленные данные, не аттестация. Изображения и вложенные пути не открываются."
    }

    Repeater {
        model: ["A", "B"]

        delegate: RowLayout {
            width: panel.width

            Label {
                text: modelData
            }

            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: index === 0 ? panel.leftFile.toString() : panel.rightFile.toString()
            }

            Button {
                text: "Выбрать…"
                enabled: !panel.controller || !panel.controller.busy
                onClicked: {
                    panel.selectingLeft = index === 0;
                    files.item.open(false, false);
                }
            }

        }

    }

    RowLayout {
        Button {
            objectName: "comparisonStart"
            text: "Сравнить отчёты"
            enabled: panel.controller && !panel.controller.busy && !panel.researchBusy && panel.leftFile.toString().length > 0 && panel.rightFile.toString().length > 0
            onClicked: panel.controller.compare(panel.leftFile, panel.rightFile)
        }

        Button {
            objectName: "comparisonCancel"
            text: "Отменить"
            enabled: panel.controller && panel.controller.busy
            onClicked: panel.controller.cancel()
        }

        Button {
            objectName: "comparisonSave"
            text: "Сохранить сравнение…"
            enabled: panel.controller && !panel.controller.busy && panel.comparisonData.rows !== undefined
            onClicked: files.item.save()
        }

    }

    BusyIndicator {
        running: panel.controller ? panel.controller.busy : false
        visible: running
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: panel.controller ? panel.controller.status : "Сравнение недоступно"
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        visible: panel.comparisonData.rows !== undefined
        text: "Загруженная пара (до следующего сравнения):\nA: " + (panel.comparisonData.left_path || "—") + "\nB: " + (panel.comparisonData.right_path || "—")
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        visible: panel.comparisonData.rows !== undefined
        text: "Inputs: " + (panel.comparisonData.inputs || "—") + "\nНаблюдение: " + (panel.comparisonData.observation_gate || "—") + "\nTimings: " + (panel.comparisonData.timing_gate || "—") + "\nQuality: " + (panel.comparisonData.quality_gate || "—") + "\nПричины: " + (panel.comparisonData.reasons ? panel.comparisonData.reasons.join(", ") : "—")
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        visible: panel.comparisonData.left !== undefined
        text: "A: " + panel.platform(panel.comparisonData.left)
    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        visible: panel.comparisonData.right !== undefined
        text: "B: " + panel.platform(panel.comparisonData.right)
    }

    Repeater {
        model: panel.comparisonData.rows || []

        delegate: GroupBox {
            width: panel.width
            title: "A[" + (modelData.left_index === null || modelData.left_index === undefined ? "—" : modelData.left_index) + "] ↔ B[" + (modelData.right_index === null || modelData.right_index === undefined ? "—" : modelData.right_index) + "] · " + modelData.pairing

            Column {
                width: parent.width
                spacing: 6

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "A: " + panel.summary(modelData.left)
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "B: " + panel.summary(modelData.right)
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "B−A: p50=" + panel.metric(modelData.timing_delta_ms) + " мс · MAE=" + panel.metric(modelData.mae_delta) + " · RMSE=" + panel.metric(modelData.rmse_delta)
                }

                CheckBox {
                    id: details

                    text: "Полные применённые настройки"
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: details.checked
                    text: panel.fields(modelData.settings, "").join("\n")
                }

            }

        }

    }

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: "Разницы B−A — наблюдения paused кадров, не FPS и не общий рейтинг. Независимость truth заявляет автор. OS/CPU/RAM passport в этих reports не аттестован; для cross-platform вывода нужны отдельные стендовые данные."
    }

}
