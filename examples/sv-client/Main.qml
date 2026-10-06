import QtQuick 2.6
import QtQuick.Window 2.2

Window {
    id: window

    property bool showServerInfo: false
    minimumWidth: 800
    minimumHeight: 480

    width: 1120
    height: 760
    visible: true
    color: "#0d121b"
    title: "Surround View — автомобильный клиент"

    Text {
        x: 28
        y: 24
        text: "SURROUND VIEW"
        color: "#edf2fa"
        font.pixelSize: 24
        font.bold: true
    }

    Text {
        x: 28
        y: 60
        text: "Поворот касанием · крупные кнопки для выбора ракурса и масштаба"
        color: "#99a8bc"
        font.pixelSize: 14
    }

    Rectangle {
        x: 24
        y: 98
        width: parent.width - 48
        height: parent.height - 240
        color: "#dbe8f2"
        radius: 12
        clip: true

        Image {
            id: frame

            anchors.fill: parent
            anchors.margins: 6
            fillMode: Image.PreserveAspectFit
            cache: false
            source: backend.frameUrl
            onStatusChanged: {
                if (status === Image.Ready) {
                    backend.imageReady(source.toString());
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: frame.source.toString() === ""
            text: "Ожидание видеоданных"
            color: "#a8b7c9"
            font.pixelSize: 20
        }

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
            onWheel: backend.zoom(-wheel.angleDelta.y / 480)

            Timer {
                interval: 33
                running: true
                repeat: true
                onTriggered: {
                    if (orbitArea.deltaX !== 0 || orbitArea.deltaY !== 0) {
                        backend.orbit(orbitArea.deltaX * 0.005, -orbitArea.deltaY * 0.005);
                        orbitArea.deltaX = 0;
                        orbitArea.deltaY = 0;
                    }
                }
            }

        }

    }

    Row {
        x: 24
        y: parent.height - 116
        spacing: 10

        Repeater {
            model: [{
                "label": "Сверху",
                "cmd": "top"
            }, {
                "label": "Спереди",
                "cmd": "front"
            }, {
                "label": "Сзади",
                "cmd": "rear"
            }, {
                "label": "Ближе",
                "cmd": "closer"
            }, {
                "label": "Дальше",
                "cmd": "farther"
            }, {
                "label": "Сервер",
                "cmd": "info"
            }]

            Rectangle {
                width: (window.width - 98) / 6
                height: 64
                radius: 7
                color: button.containsMouse ? "#315275" : "#203247"

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: "#eef4fb"
                    font.pixelSize: 18
                }

                MouseArea {
                    id: button

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (index < 3)
                            backend.preset(modelData.cmd);
                        else if (modelData.cmd === "closer")
                            backend.zoom(-0.5);
                        else if (modelData.cmd === "farther")
                            backend.zoom(0.5);
                        else {
                            window.showServerInfo = !window.showServerInfo;
                            backend.action("state");
                        }
                    }
                }

            }

        }

    }

    Rectangle {
        visible: window.showServerInfo
        anchors.centerIn: parent
        width: parent.width - 100
        height: 220
        radius: 12
        color: "#203247"
        Text {
            anchors.fill: parent
            anchors.margins: 28
            color: "#eef4fb"
            font.pixelSize: 22
            wrapMode: Text.WordWrap
            text: "Состояние сервера\n" + backend.serverInfo + "\n" + backend.status
        }
    }

    Text {
        x: 26
        y: parent.height - 38
        text: backend.status
        color: "#9bc8bb"
        font.pixelSize: 13
    }

}
