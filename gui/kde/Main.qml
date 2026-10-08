import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 900
    height: 620
    visible: true
    title: qsTr("SCSKiller")

    pageStack.initialPage: Kirigami.Page {
        title: qsTr("Pipeline preparation")

        Column {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing

            Controls.Label {
                text: qsTr("Experimental Linux build")
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.4
            }

            Controls.Label {
                text: qsTr("Native Vulkan, DXVK and vkd3d-proton support is being developed here.")
                wrapMode: Text.WordWrap
                width: Math.min(parent.width, 600)
            }

            Controls.Button {
                text: qsTr("Vulkan recorder status")
                onClicked: status.text = qsTr("Runtime recorder: experimental")
            }

            Controls.Label {
                id: status
                text: qsTr("Ready")
            }
        }
    }
}