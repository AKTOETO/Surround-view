import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Pane {
    id: panel

    property var controller: researchController
    property var selectedModes: ["multi_band", "graph_cut_multi_band"]
    property int variantCount: {
        var count = selectedModes.length;
        if (boundaryChoice.currentIndex === 2) {
            if (selectedModes.indexOf("multi_band") !== -1)
                ++count;

            if (selectedModes.indexOf("graph_cut_multi_band") !== -1)
                ++count;

        }
        return count;
    }

    function changeMode(mode, checked) {
        var values = selectedModes.slice(0);
        var index = values.indexOf(mode);
        if (checked && index === -1)
            values.push(mode);

        if (!checked && index !== -1)
            values.splice(index, 1);

        selectedModes = values;
    }

    function draft() {
        return {
            "modes": selectedModes,
            "frames": framesCount.value,
            "warmup": warmupCount.value,
            "repeats": repeatCount.value,
            "seed": seedValue.value,
            "boundary": ["zero", "normalized", "both"][boundaryChoice.currentIndex],
            "pyramid_levels": levelsCount.value,
            "seam_solver": solverChoice.currentIndex === 0 ? "binary_pairs" : "alpha_expansion"
        };
    }

    padding: 0
    font.pixelSize: 14
    palette.window: "#1a1f2c"
    palette.windowText: "#e2e8f0"
    palette.text: "#e2e8f0"
    palette.base: "#111a26"
    palette.button: "#263244"
    palette.buttonText: "#e2e8f0"
    palette.highlight: "#00d1b2"
    palette.highlightedText: "#071b1b"

    background: Rectangle {
        color: "transparent"
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: "Исследования"
            color: "#e2e8f0"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#aab8cc"
            text: "Сравнение серверных методов на replay-записи. Каждый запуск начинает запись заново; текущие поверхность и виртуальный ракурс сохраняются."
        }

        TabBar {
            id: tabs

            objectName: "researchTabs"
            width: parent.width

            TabButton {
                text: "План"
            }

            TabButton {
                text: "Выполнение"
            }

            TabButton {
                text: "Результаты"
            }

        }

        Column {
            width: parent.width
            spacing: 12
            visible: tabs.currentIndex === 0
            enabled: !panel.controller.busy

            GroupBox {
                title: "Алгоритмы"
                width: parent.width

                GridLayout {
                    width: parent.width
                    columns: width > 650 ? 2 : 1

                    Repeater {
                        model: ["edge_feather", "hard_best_angle", "angular_feather", "seam_distance_feather", "graph_cut_seam", "multi_band", "graph_cut_multi_band"]

                        CheckBox {
                            objectName: "researchMethod_" + modelData
                            text: modelData
                            checked: panel.selectedModes.indexOf(modelData) !== -1
                            onToggled: panel.changeMode(modelData, checked)
                        }

                    }

                }

            }

            GroupBox {
                title: "Кадры и повторения"
                width: parent.width

                GridLayout {
                    width: parent.width
                    columns: 4
                    columnSpacing: 16

                    Label {
                        text: "Кадров"
                    }

                    SpinBox {
                        id: framesCount

                        objectName: "researchFrames"
                        from: 1
                        to: 256
                        value: 2
                        editable: true
                    }

                    Label {
                        text: "Warmup блоков"
                    }

                    SpinBox {
                        id: warmupCount

                        objectName: "researchWarmup"
                        from: 0
                        to: 100
                        value: 2
                        editable: true
                    }

                    Label {
                        text: "Measured блоков"
                    }

                    SpinBox {
                        id: repeatCount

                        objectName: "researchRepeats"
                        from: 1
                        to: 1000
                        value: 3
                        editable: true
                    }

                    Label {
                        text: "Seed порядка"
                    }

                    SpinBox {
                        id: seedValue

                        objectName: "researchSeed"
                        from: 0
                        to: 2.14748e+09
                        value: 17
                        editable: true
                        Layout.fillWidth: true
                    }

                }

            }

            GroupBox {
                title: "Пирамида и швы"
                width: parent.width

                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 16

                    Label {
                        text: "Граница пирамиды"
                    }

                    ComboBox {
                        id: boundaryChoice

                        objectName: "researchBoundary"
                        model: ["zero", "normalized", "Сравнить обе"]
                        currentIndex: 2
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "Уровней пирамиды"
                    }

                    SpinBox {
                        id: levelsCount

                        from: 1
                        to: 8
                        value: 4
                        editable: true
                    }

                    Label {
                        text: "Решатель шва"
                    }

                    ComboBox {
                        id: solverChoice

                        model: ["binary_pairs", "alpha_expansion"]
                        Layout.fillWidth: true
                    }

                }

            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#64d2ff"
                text: panel.variantCount + " вариантов × " + framesCount.value + " кадров × " + (warmupCount.value + repeatCount.value) + " блоков = " + (panel.variantCount * framesCount.value * (warmupCount.value + repeatCount.value)) + " trials (предел 10000). Seed меняет порядок методов, а не сцену."
            }

            Label {
                text: "Новый каталог результата"
                color: "#cbd5e0"
            }

            RowLayout {
                width: parent.width

                TextField {
                    id: outputDirectory

                    objectName: "researchOutput"
                    Layout.fillWidth: true
                    text: panel.controller.suggestOutput()
                    selectByMouse: true
                }

                Button {
                    text: "Новое имя"
                    onClicked: outputDirectory.text = panel.controller.suggestOutput()
                }

            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#aab8cc"
                text: "Родительская папка должна существовать. Сценарий, RGBA и отчёт сохраняются без перезаписи. Внешний эталон в этой форме пока не задаётся: MAE/PSNR и приёмка качества не оцениваются."
            }

            Button {
                objectName: "researchStart"
                text: "Начать исследование"
                enabled: panel.controller.available && panel.variantCount > 0 && panel.variantCount * framesCount.value * (warmupCount.value + repeatCount.value) <= 10000
                onClicked: {
                    if (panel.controller.startDraft(panel.draft(), outputDirectory.text))
                        tabs.currentIndex = 1;

                }
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                visible: !panel.controller.available
                text: "Дождитесь подключения и завершения текущих команд конфигурации на основной странице."
                color: "#ffcc80"
            }

        }

        Column {
            width: parent.width
            spacing: 12
            visible: tabs.currentIndex === 1

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#e2e8f0"
                text: panel.controller.status
            }

            ProgressBar {
                width: parent.width
                from: 0
                to: Math.max(1, panel.controller.total)
                value: panel.controller.completed
            }

            Label {
                text: panel.controller.completed + " / " + panel.controller.total + " trials"
                color: "#64d2ff"
            }

            Button {
                objectName: "researchCancel"
                text: "Отменить и восстановить настройки"
                enabled: panel.controller.busy
                onClicked: panel.controller.cancel()
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#aab8cc"
                text: "Отмена ждёт текущую операцию и restore fusion/surface/pause. Предыдущий replay cursor и GPU caches не восстанавливаются. Изменение ракурса и конфигурации из этого окна заблокировано до завершения."
            }

            Button {
                text: "Показать результаты"
                enabled: !panel.controller.busy
                onClicked: tabs.currentIndex = 2
            }

        }

        Column {
            width: parent.width
            spacing: 10
            visible: tabs.currentIndex === 2

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: panel.controller.status
                color: "#e2e8f0"
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#aab8cc"
                text: "Render wall p50/p95 — задержки отдельных paused кадров в мс, не FPS. Warmup исключён; число samples включает повторные измерения одного кадра. Частичный отчёт не подтверждает успешность исследования."
            }

            Repeater {
                model: panel.controller.rows

                delegate: Rectangle {
                    width: panel.width
                    height: 66
                    radius: 6
                    color: "#263244"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 4

                        Label {
                            text: modelData.mode + " / " + modelData.boundary
                            color: "#e2e8f0"
                            font.bold: true
                        }

                        Label {
                            text: "Render p50: " + modelData.p50 + " мс  ·  p95: " + modelData.p95 + " мс  ·  n=" + modelData.measured + "  ·  MAE RGB8: " + modelData.mae
                            color: "#cbd5e0"
                        }

                    }

                }

            }

            Label {
                text: "Полный отчёт"
                color: "#cbd5e0"
            }

            TextField {
                width: parent.width
                text: panel.controller.reportPath
                readOnly: true
                selectByMouse: true
            }

            Label {
                visible: panel.controller.rows.length === 0
                text: "Нет измерений для отображения."
                color: "#aab8cc"
            }

        }

    }

}
