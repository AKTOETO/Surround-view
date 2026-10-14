import QtQuick 2.15
import QtQuick3D 6.3

Item {
    id: root
    focus: true
    property real vehicleX: 0
    property real vehicleZ: 0
    property real headingRad: 0
    property real speed: 0
    property bool forward: false
    property bool reverse: false
    property bool steerLeft: false
    property bool steerRight: false

    signal exitRequested()

    function resetInput() {
        forward = reverse = steerLeft = steerRight = false
    }
    function handleKey(key, pressed) {
        if (key === Qt.Key_W || key === Qt.Key_Up) forward = pressed
        else if (key === Qt.Key_S || key === Qt.Key_Down) reverse = pressed
        else if (key === Qt.Key_A || key === Qt.Key_Left) steerLeft = pressed
        else if (key === Qt.Key_D || key === Qt.Key_Right) steerRight = pressed
        else if (key === Qt.Key_Escape) {
            if (pressed) { resetInput(); exitRequested() }
        } else return false
        return true
    }
    function advanceVehicle(dt) {
        var acceleration = (forward ? 7.0 : 0.0) - (reverse ? 5.0 : 0.0)
        speed = Math.max(-4.0, Math.min(12.0, speed + acceleration * dt))
        if (!forward && !reverse) speed *= Math.max(0, 1 - 1.8 * dt)
        var steering = (steerRight ? 1.0 : 0.0) - (steerLeft ? 1.0 : 0.0)
        headingRad += steering * speed * Math.tan(0.45) / 1.9 * dt
        vehicleX += Math.sin(headingRad) * speed * dt
        vehicleZ -= Math.cos(headingRad) * speed * dt
    }
    onActiveFocusChanged: if (!activeFocus) resetInput()
    onVisibleChanged: if (!visible) resetInput()

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#a9c5dc"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: world
            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(root.vehicleX - Math.sin(root.headingRad) * 10, 6,
                                      root.vehicleZ + Math.cos(root.headingRad) * 10)
                eulerRotation: Qt.vector3d(-28, -root.headingRad * 180 / Math.PI, 0)
                clipNear: 0.05
                clipFar: 500
                fieldOfView: 52
            }
            DirectionalLight {
                eulerRotation: Qt.vector3d(-42, -28, 0)
                brightness: 1.25
                castsShadow: true
            }

            Model {
                source: "#Cube"
                position: Qt.vector3d(0, -0.11, 0)
                scale: Qt.vector3d(0.32, 0.002, 1.4)
                materials: PrincipledMaterial { baseColor: "#52745e"; roughness: 1 }
            }
            Model {
                source: "#Cube"
                position: Qt.vector3d(0, -0.04, 0)
                scale: Qt.vector3d(0.08, 0.0008, 1.4)
                materials: PrincipledMaterial { baseColor: "#343a42"; roughness: 1 }
            }

            Repeater3D {
                model: 18
                delegate: Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0.011, (index - 9) * 4)
                    scale: Qt.vector3d(0.0012, 0.0002, 0.02)
                    materials: PrincipledMaterial { baseColor: "#e4d9ae"; roughness: 1 }
                }
            }

            Repeater3D {
                model: 24
                delegate: Model {
                    property real side: index % 2 === 0 ? -1 : 1
                    property real row: Math.floor(index / 2)
                    property real buildingHeight: 2.5 + (index * 7 % 6)
                    source: "#Cube"
                    position: Qt.vector3d(side * (7.5 + (row % 3) * 1.4), buildingHeight / 2 - 0.01,
                                          (row - 6) * 10 + (row % 2) * 3)
                    scale: Qt.vector3d(0.03, buildingHeight / 100, 0.055)
                    materials: PrincipledMaterial {
                        baseColor: ["#b98f75", "#c7bda4", "#7f9293", "#a77c6c"][index % 4]
                        roughness: 0.9
                    }
                }
            }

            Node {
                id: vehicle
                objectName: "driveVehicle"
                position: Qt.vector3d(root.vehicleX, 0, root.vehicleZ)
                eulerRotation: Qt.vector3d(0, -root.headingRad * 180 / Math.PI, 0)

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0.55, 0)
                    objectName: "driveVehicleBody"
                    scale: Qt.vector3d(0.0155, 0.0042, 0.031)
                    materials: PrincipledMaterial { baseColor: "#d94f3d"; roughness: 0.42; metalness: 0.12 }
                }
                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0.94, -0.12)
                    scale: Qt.vector3d(0.0125, 0.0045, 0.0145)
                    materials: PrincipledMaterial { baseColor: "#88b8c9"; roughness: 0.25; metalness: 0.18 }
                }
                Repeater3D {
                    model: 4
                    delegate: Model {
                        property real side: index % 2 === 0 ? -1 : 1
                        property real axle: index < 2 ? -1 : 1
                        source: "#Cylinder"
                        position: Qt.vector3d(side * 0.81, 0.31, axle * 0.95)
                        eulerRotation: Qt.vector3d(0, 0, 90)
                        scale: Qt.vector3d(0.006, 0.0016, 0.006)
                        materials: PrincipledMaterial { baseColor: "#202329"; roughness: 0.9 }
                    }
                }
                Repeater3D {
                    model: 4
                    delegate: Model {
                        property real side: index % 2 === 0 ? -1 : 1
                        property real axle: index < 2 ? -1 : 1
                        source: "#Sphere"
                        position: Qt.vector3d(side * 0.68, 1.16, axle * 1.22)
                        scale: Qt.vector3d(0.0013, 0.0013, 0.0013)
                        materials: PrincipledMaterial { baseColor: "#ffd65a"; emissiveFactor: Qt.vector3d(0.65, 0.48, 0.12) }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) {
            root.forceActiveFocus()
            mouse.accepted = true
        }
    }

    Timer {
        interval: 16
        repeat: true
        running: root.visible
        onTriggered: root.advanceVehicle(interval / 1000.0)
    }

    Keys.onPressed: function(event) {
        if (root.handleKey(event.key, true)) event.accepted = true
    }
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat && root.handleKey(event.key, false)) event.accepted = true
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 18
        width: 310
        height: 112
        radius: 8
        color: "#cc101820"
        Text {
            anchors.fill: parent
            anchors.margins: 14
            color: "white"
            font.pixelSize: 15
            wrapMode: Text.WordWrap
            text: "Синтетический 3D-полигон\nW/S или ↑/↓ — газ/тормоз · A/D или ←/→ — руль\nСкорость: " + Math.round(root.speed * 3.6) + " км/ч · Esc — выход"
        }
    }
}
