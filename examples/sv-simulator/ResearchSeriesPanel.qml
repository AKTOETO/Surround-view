import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel
    objectName: "researchSeriesPanel"
    property var controller: null
    property bool researchBusy: false
    property var reports: []
    property var seriesData: controller && controller.series ? controller.result : ({})
    spacing: 10

    function addReport(file) {
        var text = file.toString();
        if (reports.length >= 8 || reports.indexOf(text) !== -1)
            return false;
        var next = reports.slice(0);
        next.push(text);
        reports = next;
        return true;
    }
    function removeReport(index) {
        var next = reports.slice(0);
        next.splice(index, 1);
        reports = next;
    }
    function moveReport(index, offset) {
        var destination = index + offset;
        if (destination < 0 || destination >= reports.length)
            return;
        var next = reports.slice(0);
        var value = next.splice(index, 1)[0];
        next.splice(destination, 0, value);
        reports = next;
    }
    function metric(value) {
        return value === null || value === undefined ? "—" : Number(value).toPrecision(6);
    }
    function summary(name, value) {
        return name + ": " + metric(value.minimum) + " / " + metric(value.median) + " / " + metric(value.maximum)
                + " · сопоставимы " + value.eligible_reports + "/" + value.required_reports;
    }

    Loader {
        id: files
        objectName: "seriesFilesLoader"
        visible: false
        source: simulatorQtMajorVersion >= 6 ? "ResearchFilesQt6.qml" : "ResearchFilesQt5.qml"
        onLoaded: {
            item.openTitle = "Добавить individual research report";
            item.saveTitle = "Сохранить сводку серии в новый файл";
            item.chosen.connect(function(file, reference) { panel.addReport(file); });
            item.saveChosen.connect(function(file) { if (panel.controller) panel.controller.save(file); });
        }
    }
    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: "Выберите 2–8 individual отчётов. Первый — базовый; стрелки меняют порядок. batch.json не подходит. Сводка показывает минимум / медиану / максимум по отчётам, без объединения кадров и доверительных интервалов."
    }
    Column {
        width: parent.width
        spacing: 5
        enabled: !panel.researchBusy && !(panel.controller && panel.controller.busy)
        Repeater {
            model: panel.reports
            delegate: RowLayout {
                width: parent.width
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    text: (index === 0 ? "Базовый: " : (index + 1) + ". ") + modelData
                }
                Button { text: "↑"; enabled: index > 0; onClicked: panel.moveReport(index, -1) }
                Button { text: "↓"; enabled: index + 1 < panel.reports.length; onClicked: panel.moveReport(index, 1) }
                Button { text: "Удалить"; onClicked: panel.removeReport(index) }
            }
        }
        Button {
            objectName: "seriesAddReport"
            text: "Добавить отчёт…"
            enabled: panel.reports.length < 8
            onClicked: files.item.open(false, false)
        }
    }
    RowLayout {
        Button {
            objectName: "seriesCompare"
            text: "Сравнить серию"
            enabled: panel.controller && !panel.controller.busy && !panel.researchBusy && panel.reports.length >= 2
            onClicked: panel.controller.compareSeries(panel.reports)
        }
        Button {
            objectName: "seriesCancel"
            text: "Отменить"
            enabled: panel.controller && panel.controller.busy
            onClicked: panel.controller.cancel()
        }
        Button {
            objectName: "seriesSave"
            text: "Сохранить сводку…"
            enabled: panel.controller && panel.controller.series && !panel.controller.busy && panel.seriesData.rows !== undefined
            onClicked: files.item.save()
        }
    }
    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: panel.controller ? panel.controller.status : "Сравнение недоступно"
    }
    Label {
        width: parent.width
        wrapMode: Text.Wrap
        visible: panel.seriesData.inputs !== undefined
        text: "Загружено: " + (panel.seriesData.report_count || 0) + " отчётов. Базовый: "
              + (panel.seriesData.inputs && panel.seriesData.inputs.length ? panel.seriesData.inputs[0].path : "—")
              + ". Изменение списка выше не меняет ранее загруженную сводку."
    }
    Repeater {
        model: panel.seriesData.rows || []
        delegate: GroupBox {
            width: parent.width
            title: "Вариант базового отчёта " + modelData.baseline_variant_index
            Column {
                width: parent.width
                spacing: 5
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: modelData.settings.fusion.mode + " · " + (modelData.settings.surface ? modelData.settings.surface.type : "носитель не подтверждён")
                }
                Label { text: panel.summary("Render p50, мс", modelData.render_p50_ms) }
                Label { text: panel.summary("MAE RGB8", modelData.mae_rgb8) }
                Label { text: panel.summary("RMSE RGB8", modelData.rmse_rgb8) }
            }
        }
    }
    Repeater {
        model: panel.seriesData.comparisons || []
        delegate: Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Базовый ↔ отчёт " + (modelData.report_index + 1)
                + ": время — " + modelData.comparison.timing_gate
                + "; качество — " + modelData.comparison.quality_gate
                + "; причины: " + (modelData.comparison.reasons.length ? modelData.comparison.reasons.join(", ") : "нет")
        }
    }
    Label {
        width: parent.width
        wrapMode: Text.Wrap
        text: "Агрегат появляется только при сопоставимости всех отчётов для этой метрики. Недопустимые запуски не отбрасываются молча. Совпадение данных не подтверждает независимость опытов, научную приёмку или общий рейтинг. Полные настройки и попарные raw metrics сохраняются в сводке."
    }
}
