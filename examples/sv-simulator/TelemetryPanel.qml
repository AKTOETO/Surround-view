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
    TabBar {
        id: tabs; width: parent.width; objectName: "telemetryTabs"
        TabButton { text: "CPU" }
        TabButton { text: "Память" }
        TabButton { text: "Render" }
    }
    StackLayout {
        width: parent.width; height: 155; currentIndex: tabs.currentIndex
        TelemetryChart { points: panel.model.points; metric: "cpu"; title: "CPU, % (100% = одно ядро)" }
        TelemetryChart { points: panel.model.points; metric: "rss"; title: "Current RSS, MiB (приближённо)" }
        TelemetryChart { points: panel.model.points; metric: "render"; title: "Render wall, мс (fresh frames)" }
    }
    Text {
        width: parent.width; color: "#8e9bb0"; wrapMode: Text.Wrap
        text: "120 последних samples. Пропуски и задержки разрывают линии. Квантили — только свежие выбранные кадры в окне, не все кадры сервера. Чтение не включает сбор: настройте его во вкладке Конфигурация → Телеметрия."
    }
}
