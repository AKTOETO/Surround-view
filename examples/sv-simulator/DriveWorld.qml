import QtQuick 2.15
import QtQuick3D 6.3

Item {
    id: root
    focus: true
    property real vehicleX: 0
    property real vehicleZ: 0
    property real heading: 0
    property real speed: 0
    property bool forward: false
    property bool reverse: false
    property bool left: false
    property bool right: false

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
                position: Qt.vector3d(root.vehicleX + 14, 18, root.vehicleZ + 19)
                eulerRotation: Qt.vector3d(-34, 0, 0)
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
                position: Qt.vector3d(0, -0.35, 0)
                scale: Qt.vector3d(18, 0.2, 70)
                materials: PrincipledMaterial { baseColor: "#52745e"; roughness: 1 }
            }
            Model {
                source: "#Cube"
                position: Qt.vector3d(0, -0.18, 0)
                scale: Qt.vector3d(8, 0.08, 70)
                materials: PrincipledMaterial { baseColor: "#343a42"; roughness: 1 }
            }

            Repeater3D {
                model: 18
                delegate: Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, -0.125, (index - 9) * 4)
                    scale: Qt.vector3d(0.12, 0.02, 2.0)
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
                    position: Qt.vector3d(side * (7.5 + (row % 3) * 1.4), buildingHeight / 2 - 0.25,
                                          (row - 6) * 10 + (row % 2) * 3)
                    scale: Qt.vector3d(3.0, buildingHeight, 5.5)
                    materials: PrincipledMaterial {
                        baseColor: ["#b98f75", "#c7bda4", "#7f9293", "#a77c6c"][index % 4]
                        roughness: 0.9
                    }
                }
            }

            Node {
                id: vehicle
                position: Qt.vector3d(root.vehicleX, 0, root.vehicleZ)
                eulerRotation: Qt.vector3d(0, root.heading, 0)

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0.55, 0)
                    scale: Qt.vector3d(1.55, 0.42, 3.1)
                    materials: PrincipledMaterial { baseColor: "#d94f3d"; roughness: 0.42; metalness: 0.12 }
                }
                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0.94, -0.12)
                    scale: Qt.vector3d(1.25, 0.45, 1.45)
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
                        scale: Qt.vector3d(0.30, 0.16, 0.30)
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
                        scale: Qt.vector3d(0.13, 0.13, 0.13)
                        materials: PrincipledMaterial { baseColor: "#ffd65a"; emissiveFactor: Qt.vector3d(0.65, 0.48, 0.12) }
                    }
                }
            }
        }
    }

    Timer {
        interval: 16
        repeat: true
        running: true
        onTriggered: {
            var dt = interval / 1000.0;
            var acceleration = (root.forward ? 7.0 : 0.0) - (root.reverse ? 5.0 : 0.0);
            root.speed = Math.max(-4.0, Math.min(12.0, root.speed + acceleration * dt));
            if (!root.forward && !root.reverse)
                root.speed *= Math.max(0, 1 - 1.8 * dt);
            var steering = (root.right ? 1.0 : 0.0) - (root.left ? 1.0 : 0.0);
            root.heading += steering * root.speed * 2.3 * dt / 12.0;
            root.vehicleX += Math.sin(root.heading) * root.speed * dt;
            root.vehicleZ -= Math.cos(root.heading) * root.speed * dt;
        }
    }

    Keys.onPressed: {
        if (event.key === Qt.Key_W || event.key === Qt.Key_Up) root.forward = true;
        else if (event.key === Qt.Key_S || event.key === Qt.Key_Down) root.reverse = true;
        else if (event.key === Qt.Key_A || event.key === Qt.Key_Left) root.left = true;
        else if (event.key === Qt.Key_D || event.key === Qt.Key_Right) root.right = true;
        else if (event.key === Qt.Key_Escape) root.visible = false;
    }
    Keys.onReleased: {
        if (event.key === Qt.Key_W || event.key === Qt.Key_Up) root.forward = false;
        else if (event.key === Qt.Key_S || event.key === Qt.Key_Down) root.reverse = false;
        else if (event.key === Qt.Key_A || event.key === Qt.Key_Left) root.left = false;
        else if (event.key === Qt.Key_D || event.key === Qt.Key_Right) root.right = false;
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
