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
    property bool showDriveWorld: startDrivingWorld

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
            visible: !window.showDriveWorld
        }

        Text {
            anchors.centerIn: parent
            visible: !window.showDriveWorld && frameImage.source.toString() === ""
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
            visible: !window.showDriveWorld
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

        Loader {
            anchors.fill: parent
            active: window.showDriveWorld
            visible: active
            source: driveWorldAvailable ? "qrc:/DriveWorld.qml" : ""
            onLoaded: item.forceActiveFocus()
        }
    }

    // Right Control Sidebar
    Rectangle {
        x: parent.width - 340
        y: 52
        width: 340
        height: parent.height - 52
        color: "#1a1f2c"

        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: controlsColumn.height + 32
            clip: true

            Column {
                id: controlsColumn
                x: 16
                y: 16
                width: parent.width - 32
                height: childrenRect.height
                spacing: 12

            Text {
                text: "ПОДКЛЮЧЕНИЕ К СЕРВЕРУ"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Text { text: "Локальный IPC каталог"; color: "#718096"; font.pixelSize: 11 }
            Rectangle {
                width: parent.width; height: 34; radius: 5; color: "#111620"
                border.color: "#384358"
                TextInput {
                    id: ipcDirectory
                    anchors.fill: parent; anchors.margins: 8
                    text: bridge.unixDirectory
                    color: "#e2e8f0"; font.pixelSize: 12
                    selectByMouse: true
                }
            }
            Row {
                spacing: 8
                Rectangle {
                    width: 145; height: 34; radius: 5
                    color: findMouse.containsMouse ? "#176b68" : "#155451"
                    Text { anchors.centerIn: parent; text: "Найти IPC"; color: "white"; font.pixelSize: 12 }
                    MouseArea {
                        id: findMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: bridge.discoverLocal(timeoutInput.text, reconnectInput.text)
                    }
                }
                Rectangle {
                    width: 145; height: 34; radius: 5
                    color: unixMouse.containsMouse ? "#2b3448" : "#222a3a"
                    Text { anchors.centerIn: parent; text: "Подключить Unix"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea {
                        id: unixMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: bridge.connectUnix(ipcDirectory.text, timeoutInput.text, reconnectInput.text, retriesInput.text)
                    }
                }
            }

            Text { text: "Удалённый TCP endpoint"; color: "#718096"; font.pixelSize: 11 }
            Row {
                spacing: 5
                Rectangle {
                    width: 142; height: 34; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: tcpHost; anchors.fill: parent; anchors.margins: 7; text: bridge.tcpHost; color: "#e2e8f0"; font.pixelSize: 12; selectByMouse: true }
                }
                Rectangle {
                    width: 78; height: 34; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: controlPort; anchors.fill: parent; anchors.margins: 7; text: String(bridge.controlPort); color: "#e2e8f0"; font.pixelSize: 12; selectByMouse: true; validator: IntValidator { bottom: 1; top: 65535 } }
                }
                Rectangle {
                    width: 78; height: 34; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: dataPort; anchors.fill: parent; anchors.margins: 7; text: String(bridge.dataPort); color: "#e2e8f0"; font.pixelSize: 12; selectByMouse: true; validator: IntValidator { bottom: 1; top: 65535 } }
                }
            }
            Row {
                spacing: 8
                Rectangle {
                    width: 145; height: 34; radius: 5
                    color: tcpMouse.containsMouse ? "#2b3448" : "#222a3a"
                    Text { anchors.centerIn: parent; text: "Подключить TCP"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea {
                        id: tcpMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: bridge.connectTcp(tcpHost.text, controlPort.text, dataPort.text, timeoutInput.text, reconnectInput.text, retriesInput.text)
                    }
                }
                Rectangle {
                    width: 145; height: 34; radius: 5
                    color: disconnectMouse.containsMouse ? "#543333" : "#3a292d"
                    Text { anchors.centerIn: parent; text: "Отключиться"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: disconnectMouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.disconnectFromServer() }
                }
            }
            Text { text: "timeout / reconnect / retries"; color: "#718096"; font.pixelSize: 11 }
            Row {
                spacing: 5
                Rectangle {
                    width: 100; height: 32; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: timeoutInput; anchors.fill: parent; anchors.margins: 7; text: String(bridge.timeoutMs); color: "#e2e8f0"; font.pixelSize: 12; validator: IntValidator { bottom: 1; top: 60000 } }
                }
                Rectangle {
                    width: 100; height: 32; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: reconnectInput; anchors.fill: parent; anchors.margins: 7; text: String(bridge.reconnectMs); color: "#e2e8f0"; font.pixelSize: 12; validator: IntValidator { bottom: 1; top: 60000 } }
                }
                Rectangle {
                    width: 100; height: 32; radius: 5; color: "#111620"; border.color: "#384358"
                    TextInput { id: retriesInput; anchors.fill: parent; anchors.margins: 7; text: String(bridge.maxRetries); color: "#e2e8f0"; font.pixelSize: 12; validator: IntValidator { bottom: 0; top: 1000 } }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#2d3748" }

            Text {
                text: "УПРАВЛЕНИЕ РАКУРСОМ"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width
                height: 42
                radius: 6
                color: driveWorldMouse.pressed ? "#176b68" : "#155451"
                visible: driveWorldAvailable
                Text {
                    anchors.centerIn: parent
                    text: window.showDriveWorld ? "Вернуться к изображению сервера" : "Открыть 3D-мир и управлять машиной"
                    color: "white"
                    font.pixelSize: 13
                }
                MouseArea {
                    id: driveWorldMouse
                    anchors.fill: parent
                    onClicked: window.showDriveWorld = !window.showDriveWorld
                }
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

            RuntimePanel { width: parent.width }

            Text {
                text: "СЕРВЕРНАЯ КАЛИБРОВКА"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width; height: 34; radius: 5
                color: "#111620"; border.color: "#384358"
                TextInput {
                    id: calibrationJobId
                    anchors.fill: parent; anchors.margins: 8
                    color: "#e2e8f0"; font.pixelSize: 12
                    selectByMouse: true
                }
                Text {
                    anchors.fill: parent; anchors.margins: 8
                    text: "ID задачи, например calib-job-3"
                    visible: calibrationJobId.text.length === 0
                    color: "#718096"; font.pixelSize: 12
                }
            }

            Row {
                spacing: 8

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: "#202631"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Нет observations"; color: "#798496"; font.pixelSize: 12 }
                    enabled: false
                }

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: c2Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Статус"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: c2Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.checkCalibrationStatus(calibrationJobId.text) }
                }

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: cCancelMouse.containsMouse ? "#543333" : "#3a292d"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Отменить"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: cCancelMouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.cancelCalibration(calibrationJobId.text) }
                }

                Rectangle {
                    width: 98; height: 38; radius: 6
                    color: c3Mouse.containsMouse ? "#2b3448" : "#222a3a"
                    border.color: "#384358"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Применить"; color: "#e2e8f0"; font.pixelSize: 12 }
                    MouseArea { id: c3Mouse; anchors.fill: parent; hoverEnabled: true; onClicked: bridge.applyCalibration(calibrationJobId.text) }
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
                text: "ДИАГНОСТИКА СЕРВЕРА"
                color: "#8e9bb0"
                font.bold: true
                font.pixelSize: 12
            }

            Text { width: parent.width; text: bridge.pipelineInfo; color: "#cbd5e0"; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Text { width: parent.width; text: bridge.sourceInfo; color: "#cbd5e0"; font.pixelSize: 11; wrapMode: Text.WordWrap }
            }
        }
    }
}
