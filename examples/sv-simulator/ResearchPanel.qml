import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Pane {
    id: panel

    property var controller: researchController
    property var qualityLabels: ({
        "not_available": "не проверено",
        "not_requested": "критерии не заданы",
        "passed": "пройдены",
        "failed": "НЕ пройдены",
        "incomplete": "оценка не завершена"
    })
    property url scenarioSource: ""
    property url referenceSource: ""
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

    Loader {
        id: researchFiles

        objectName: "researchFilesLoader"
        visible: false
        source: simulatorQtMajorVersion >= 6 ? "ResearchFilesQt6.qml" : "ResearchFilesQt5.qml"
        onLoaded: item.chosen.connect(function(file, reference) {
            if (reference)
                panel.referenceSource = file;
            else
                panel.scenarioSource = file;
        })
    }

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
            text: "Сравнение серверных методов на replay-записи. Форма начинает запись заново; импорт следует параметрам выбранного сценария."
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

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#64d2ff"
            text: panel.controller.status
            visible: tabs.currentIndex === 0
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#cbd5e0"
            text: panel.controller.referenceInfo + " · критерии: " + (panel.qualityLabels[panel.controller.qualityStatus] || panel.controller.qualityStatus)
            visible: tabs.currentIndex !== 0
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: "#aab8cc"
            text: "regression — проверка воспроизводимости, не независимая truth. passed относится к заданным критериям; независимость reference задаёт автор и программа её не доказывает."
            visible: tabs.currentIndex === 2
        }

        Column {
            width: parent.width
            spacing: 12
            visible: tabs.currentIndex === 0
            enabled: !panel.controller.busy

            GroupBox {
                title: "Источник плана"
                width: parent.width

                Column {
                    width: parent.width
                    spacing: 8

                    ComboBox {
                        id: planMode

                        objectName: "researchPlanMode"
                        width: parent.width
                        model: ["Настроить через форму", "Запустить сохранённый сценарий"]
                    }

                    RowLayout {
                        width: parent.width
                        visible: planMode.currentIndex === 1

                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            text: panel.scenarioSource.toString()
                            placeholderText: "Сценарий не выбран"
                            selectByMouse: true
                        }

                        Button {
                            objectName: "researchChooseScenario"
                            text: "Выбрать…"
                            onClicked: researchFiles.item.open(false)
                        }

                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: planMode.currentIndex === 1
                        text: "Файл не редактируется. Его relative reference разрешается от исходного каталога; значения формы ниже не применяются. Batch-план здесь не поддерживается."
                    }

                }

            }

            GroupBox {
                visible: planMode.currentIndex === 0
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
                visible: planMode.currentIndex === 0
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
                visible: planMode.currentIndex === 0
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

            GroupBox {
                title: "Внешний эталон (необязательно)"
                width: parent.width
                visible: planMode.currentIndex === 0

                Column {
                    width: parent.width
                    spacing: 8

                    RowLayout {
                        width: parent.width

                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            text: panel.referenceSource.toString()
                            placeholderText: "Без оценки качества"
                            selectByMouse: true
                        }

                        Button {
                            objectName: "researchChooseReference"
                            text: "Выбрать…"
                            onClicked: researchFiles.item.open(true)
                        }

                        Button {
                            text: "Убрать"
                            enabled: panel.referenceSource.toString().length > 0
                            onClicked: panel.referenceSource = ""
                        }

                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "RGB/mask, guards и заранее заданные критерии читаются из manifest. Выбирайте совместимый reference: тот же кадр, ракурс, калибровка и размер. Предпочтителен schema2."
                    }

                }

            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: "#64d2ff"
                visible: planMode.currentIndex === 0
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
                text: "Родительская папка должна существовать. Отчёт сохраняется без перезаписи. Эталон проверяется до подключения к серверу; без критериев вычисленные метрики не означают приёмку качества."
            }

            Button {
                objectName: "researchStart"
                text: "Начать исследование"
                enabled: panel.controller.available && (planMode.currentIndex === 1 ? panel.scenarioSource.toString().length > 0 : panel.variantCount > 0 && panel.variantCount * framesCount.value * (warmupCount.value + repeatCount.value) <= 10000)
                onClicked: {
                    if (planMode.currentIndex === 1 ? panel.controller.startScenario(panel.scenarioSource, outputDirectory.text) : panel.controller.startDraftWithReference(panel.draft(), outputDirectory.text, panel.referenceSource))
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
                indeterminate: panel.controller.busy && panel.controller.total === 0
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
                    height: 90
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
                            text: "Render p50: " + modelData.p50 + " мс  ·  p95: " + modelData.p95 + " мс  ·  n=" + modelData.measured
                            color: "#cbd5e0"
                        }

                        Label {
                            text: "MAE RGB8: " + modelData.mae + " · RMSE RGB8: " + modelData.rmse + " · PSNR: " + modelData.psnr + " dB · frames=" + modelData.qualityFrames
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
