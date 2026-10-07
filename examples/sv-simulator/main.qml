import QtQuick 2.6
import QtQuick.Window 2.2

Window {
    id: window
    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: "sv-simulator — 3D Surround View Simulator & Control Center"
    color: "#141820"

    // Top Header Bar
    Rectangle {
        id: header
        x: 0; y: 0
        width: parent.width
        height: 52
        color: "#1e2430"

        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "sv-simulator"
            color: "#00d1b2"
            font.bold: true
            font.pixelSize: 20
        }

        Text {
            x: 160
            anchors.verticalCenter: parent.verticalCenter
            text: bridge.status
            color: "#64d2ff"
            font.pixelSize: 13
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: bridge.serverInfo
            color: "#8e9bb0"
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
        }
    }

    // Main Content Area
    Rectangle {
        x: 0
        y: 52
        width: parent.width - 340
        height: parent.height - 52
        color: "#0a0c10"

        Image {
            id: frameImage
            anchors.fill: parent
            anchors.margins: 8
            fillMode: Image.PreserveAspectFit
            cache: false
            source: bridge.frameUrl
        }

        Text {
            anchors.centerIn: parent
            visible: frameImage.source.toString() === ""
            text: "Ожидание видеоданных с сервера..."
            color: "#4a5568"
            font.pixelSize: 18
        }

        // Overlay Mouse Control for Orbit
        MouseArea {
            id: orbitArea
            property real lastX: 0
            property real lastY: 0
            property real deltaX: 0
            property real deltaY: 0

            anchors.fill: parent
            onPressed: {
                lastX = mouse.x;
                lastY = mouse.y;
            }
            onPositionChanged: {
                if (pressed) {
                    deltaX += mouse.x - lastX;
                    deltaY += mouse.y - lastY;
                    lastX = mouse.x;
                    lastY = mouse.y;
                }
            }
            onWheel: bridge.zoom(-wheel.angleDelta.y / 480)

            Timer {
                interval: 33
                running: true
                repeat: true
                onTriggered: {
                    if (orbitArea.deltaX !== 0 || orbitArea.deltaY !== 0) {
                        bridge.orbit(orbitArea.deltaX * 0.005, -orbitArea.deltaY * 0.005);
                        orbitArea.deltaX = 0;
                        orbitArea.deltaY = 0;
                    }
                }
            }
        }
    }

    // Right Control Sidebar
    Rectangle {
        x: parent.width - 340
        y: 52
        width: 340
        height: parent.height - 52
        color: "#1a1f2c"

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Text {
                text: "УПРАВЛЕНИЕ РАКУРСОМ"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Row {
                spacing: 8
                width: parent.width

                Repeater {
                    model: [
                        { label: "Сверху", cmd: "top" },
                        { label: "Спереди", cmd: "front" },
                        { label: "Сзади", cmd: "rear" }
                    ]

                    Rectangle {
                        width: 98
                        height: 38
                        radius: 6
                        color: btnMouse.containsMouse ? "#2b3448" : "#222a3a"
                        border.color: "#384358"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: "#e2e8f0"
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: btnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: bridge.preset(modelData.cmd)
                        }
                    }
                }
            }

            Row {
                spacing: 8
                width: parent.width

                Repeater {
                    model: [
                        { label: "Ближе +", action: "zoom_in" },
                        { label: "Дальше -", action: "zoom_out" }
                    ]

                    Rectangle {
                        width: 150
                        height: 38
                        radius: 6
                        color: zMouse.containsMouse ? "#2b3448" : "#222a3a"
                        border.color: "#384358"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: "#e2e8f0"
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: zMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (modelData.action === "zoom_in") bridge.zoom(-0.5);
                                else bridge.zoom(0.5);
                            }
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#2d3748" }

            Text {
                text: "ВОСПРОИЗВЕДЕНИЕ"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Row {
                spacing: 8

                Repeater {
                    model: [
                        { label: "Пауза", cmd: "pause" },
                        { label: "Старт", cmd: "resume" },
                        { label: "Шаг", cmd: "step" }
                    ]

                    Rectangle {
                        width: 98
                        height: 38
                        radius: 6
                        color: pMouse.containsMouse ? "#2b3448" : "#222a3a"
                        border.color: "#384358"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: "#e2e8f0"
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: pMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: bridge.action(modelData.cmd)
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#2d3748" }

            Text {
                text: "СЕРВЕРНАЯ КАЛИБРОВКА"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Row {
                spacing: 8

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: c1Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Запустить"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: c1Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.submitCalibration(0, "iterative") }
                }

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: c2Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Статус"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: c2Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.checkCalibrationStatus("") }
                }

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: c3Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Применить"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: c3Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.applyCalibration("") }
                }
            }

            Text {
                width: parent.width
                text: bridge.calibrationStatus
                color: "#00d1b2"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Rectangle { width: parent.width; height: 1; color: "#2d3748" }

            Text {
                text: "ИМИТАЦИЯ ДВИЖЕНИЯ ТС"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Row {
                spacing: 8
                Rectangle {
                    width: 150; height: 36; radius: 6
                    color: v1Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Вперед (5 м/с)"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: v1Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.setVehicleSpeed(5.0) }
                }
                Rectangle {
                    width: 150; height: 36; radius: 6
                    color: v2Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Стоп (0 м/с)"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: v2Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.resetVehicle() }
                }
            }
        }
    }
}
