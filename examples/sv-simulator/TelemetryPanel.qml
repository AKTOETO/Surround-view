import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Column {
    id: panel
    property var model: telemetryModel
    spacing: 8
    Text { text: "ТЕЛЕМЕТРИЯ СЕРВЕРА"; color: "#8e9bb0"; font.bold: true }
    ConfigCheck {
        objectName: "telemetryMonitoring"
        text: "Читать каждые 500 мс"
        checked: panel.model.monitoring
        onClicked: panel.model.setMonitoring(checked)
    }
    Text { width: parent.width; text: panel.model.status; color: "#64d2ff"; wrapMode: Text.Wrap }
    Text { width: parent.width; text: panel.model.summary; color: "#cbd5e0"; wrapMode: Text.Wrap }
    Button { text: "Очистить графики локально"; onClicked: panel.model.clear() }
    TextField {
        id: reportLabel; width: parent.width; maximumLength: 64
        placeholderText: "Подпись отчёта (необязательно)"
    }
    Button {
        objectName: "saveTelemetryReport"; width: parent.width
        text: "Сохранить окно в отчёт"; enabled: panel.model.canExport && files.status === Loader.Ready
        onClicked: files.item.save()
    }
    Button {
        objectName: "openTelemetryReport"; width: parent.width
        text: "Открыть отчёт offline"; enabled: panel.model.canImport && files.status === Loader.Ready
        onClicked: files.item.open()
    }
    Text {
        width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
        text: panel.model.offline ? "OFFLINE · данные из файла; сервер не изменяется" : "Для открытия отчёта сначала отключитесь от сервера. Сохраняется только текущее окно, не полный trace."
    }
    Loader {
        id: files; objectName: "telemetryFilesLoader"
        width: 0; height: 0
        source: typeof simulatorQtMajorVersion !== "undefined" && simulatorQtMajorVersion < 6 ? "TelemetryFilesQt5.qml" : "TelemetryFilesQt6.qml"
        onLoaded: item.chosen.connect(function(file, saving) {
            if (saving) panel.model.saveReport(file, reportLabel.text)
            else panel.model.loadReport(file)
        })
    }
    TabBar {
        id: tabs; width: parent.width; objectName: "telemetryTabs"
        TabButton { text: "CPU" }
        TabButton { text: "Память" }
        TabButton { text: "Render" }
        TabButton { text: "Этапы" }
        TabButton { text: "I/O" }
    }
    StackLayout {
        width: parent.width; height: tabs.currentIndex < 3 ? 155 : tabs.currentIndex === 3 ? stages.implicitHeight : io.implicitHeight; currentIndex: tabs.currentIndex
        TelemetryChart { points: panel.model.points; metric: "cpu"; title: "CPU, % (100% = одно ядро)" }
        TelemetryChart { points: panel.model.points; metric: "rss"; title: "Current RSS, MiB (приближённо)" }
        TelemetryChart { points: panel.model.points; metric: "render"; title: "Render wall, мс (fresh frames)" }
        TelemetryMetricPanel { id: stages; model: panel.model; choices: panel.model.pipelineMetrics; choiceName: "pipelineMetricChoice" }
        Column {
            id: io; spacing: 7
            TelemetryMetricPanel { width: parent.width; model: panel.model; choices: panel.model.sourceMetrics; choiceName: "sourceMetricChoice"; quantiles: false }
            Text { width: parent.width; text: panel.model.sourceDetails; color: "#cbd5e0"; wrapMode: Text.Wrap }
        }
    }
    Text {
        width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
        text: "120 последних samples. Пропуски и задержки разрывают линии. Квантили — только свежие выбранные кадры в окне, не все кадры сервера. Чтение не включает сбор: настройте его во вкладке Конфигурация → Телеметрия."
    }
}
