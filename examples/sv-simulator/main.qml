import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "sv-simulator — 3D Surround View Simulator & Control Center"
    color: "#181b20"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top Status Header Bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            color: "#21252d"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16

                Text {
                    text: "sv-simulator"
                    color: "#00d1b2"
                    font.bold: true
                    font.pixelSize: 18
                }

                Rectangle {
                    width: 1; height: 24; color: "#3a3f4d"
                }

                Text {
                    text: bridge.status
                    color: "#e0e6ed"
                    font.pixelSize: 13
                    Layout.fillWidth: true
                }

                Text {
                    text: bridge.serverInfo
                    color: "#8c9ba5"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // Main Content Area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Left Viewport Panel
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#121418"

                Image {
                    id: frameImage
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                    source: bridge.frameUrl
                    cache: false
                }

                // Overlay Controls
                Row {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: 16
                    spacing: 8

                    Button {
                        text: "Пауза"
                        onClicked: bridge.action("pause")
                    }
                    Button {
                        text: "Старт"
                        onClicked: bridge.action("resume")
                    }
                    Button {
                        text: "Шаг"
                        onClicked: bridge.action("step")
                    }
                }
            }

            // Right Control Sidebar
            Rectangle {
                implicitWidth: 360
                Layout.fillHeight: true
                color: "#1e222b"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Text {
                        text: "Управление ракурсом"
                        color: "#ffffff"
                        font.bold: true
                        font.pixelSize: 15
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            Layout.fillWidth: true
                            text: "Сверху"
                            onClicked: bridge.preset("top")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Спереди"
                            onClicked: bridge.preset("front")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Сзади"
                            onClicked: bridge.preset("rear")
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        rowSpacing: 8
                        columnSpacing: 8

                        Button {
                            Layout.fillWidth: true
                            text: " Orbit ◄"
                            onClicked: bridge.orbit(-0.15, 0.0)
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Orbit ► "
                            onClicked: bridge.orbit(0.15, 0.0)
                        }
                        Button {
                            Layout.fillWidth: true
                            text: " Orbit ▲"
                            onClicked: bridge.orbit(0.0, 0.1)
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Orbit ▼ "
                            onClicked: bridge.orbit(0.0, -0.1)
                        }
                        Button {
                            Layout.fillWidth: true
                            text: " Zoom In +"
                            onClicked: bridge.zoom(-0.5)
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Zoom Out -"
                            onClicked: bridge.zoom(0.5)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; height: 1; color: "#3a3f4d"
                    }

                    Text {
                        text: "Асинхронная калибровка"
                        color: "#ffffff"
                        font.bold: true
                        font.pixelSize: 15
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Button {
                            Layout.fillWidth: true
                            text: "Запустить (Cam 0)"
                            onClicked: bridge.submitCalibration(0, "iterative")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Статус"
                            onClicked: bridge.checkCalibrationStatus("")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Применить"
                            onClicked: bridge.applyCalibration("")
                        }
                    }

                    Text {
                        text: bridge.calibrationStatus
                        color: "#00d1b2"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true; height: 1; color: "#3a3f4d"
                    }

                    Text {
                        text: "Имитация движения ТС"
                        color: "#ffffff"
                        font.bold: true
                        font.pixelSize: 15
                    }

                    Text {
                        text: "Скорость: " + speedSlider.value.toFixed(1) + " м/с"
                        color: "#8c9ba5"
                        font.pixelSize: 12
                    }
                    Slider {
                        id: speedSlider
                        Layout.fillWidth: true
                        from: -5.0
                        to: 15.0
                        value: bridge.vehicleSpeed
                        onValueChanged: bridge.setVehicleSpeed(value)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Сброс позиции ТС"
                        onClicked: bridge.resetVehicle()
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
