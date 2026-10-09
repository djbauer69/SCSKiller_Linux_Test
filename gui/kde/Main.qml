import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 1120
    height: 900
    minimumWidth: 760
    minimumHeight: 680
    visible: true
    title: qsTr("SCSKiller — Vulkan Pipeline Preparation")

    function extraArgumentList() {
        return extraArgumentsField.text.split(/\r?\n/).filter(function (value) {
            return value.trim().length > 0
        })
    }

    function captureNative() {
        backend.runCommand("record-vulkan", [
            nativeExecutable.text,
            workDirectory.text,
            layerDirectory.text,
            capturePath.text
        ].concat(extraArgumentList()))
    }

    function captureProton() {
        backend.runCommand("record-proton", [
            protonExecutable.text,
            compatData.text,
            workDirectory.text,
            gameExecutable.text,
            layerDirectory.text,
            capturePath.text
        ].concat(extraArgumentList()))
    }

    function inspectCapture() {
        backend.runCommand("inspect-vulkan", [capturePath.text])
    }

    function warmCapture() {
        var warmArgs = [capturePath.text, "--output-cache", cachePath.text]
        if (requireComplete.checked)
            warmArgs.push("--require-complete")
        backend.runCommand("warm-vulkan", warmArgs)
    }

    function launchNative() {
        backend.runCommand("run-vulkan", [
            nativeExecutable.text,
            workDirectory.text,
            layerDirectory.text,
            cachePath.text
        ].concat(extraArgumentList()))
    }

    function launchProton() {
        backend.runCommand("run-proton-vulkan", [
            protonExecutable.text,
            compatData.text,
            workDirectory.text,
            gameExecutable.text,
            layerDirectory.text,
            cachePath.text
        ].concat(extraArgumentList()))
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        title: qsTr("Pipeline preparation")

        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                text: qsTr("Prepare Vulkan pipelines before play")
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Capture a representative run, inspect what was recorded, warm the reconstructible pipelines, then launch again with the driver cache injected. Native Vulkan, DXVK, and vkd3d-proton all meet at the Vulkan layer.")
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Warning
                visible: true
                text: qsTr("Experimental: unsupported extension state and immutable samplers may cause pipelines to be skipped. Use “Require complete replay” before assuming a capture is fully warmed. Driver cache files are specific to the current GPU/driver.")
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Capture paths")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                Controls.TextField {
                    id: nativeExecutable
                    Kirigami.FormData.label: qsTr("Native executable:")
                    Layout.fillWidth: true
                    placeholderText: qsTr("/path/to/native-vulkan-game")
                }

                Controls.TextField {
                    id: protonExecutable
                    Kirigami.FormData.label: qsTr("Proton executable:")
                    Layout.fillWidth: true
                    placeholderText: qsTr("/path/to/Proton/proton")
                }

                Controls.TextField {
                    id: compatData
                    Kirigami.FormData.label: qsTr("Proton compatdata:")
                    Layout.fillWidth: true
                    placeholderText: qsTr("/path/to/steamapps/compatdata/APPID")
                }

                Controls.TextField {
                    id: gameExecutable
                    Kirigami.FormData.label: qsTr("Windows game executable:")
                    Layout.fillWidth: true
                    placeholderText: qsTr("Game.exe (relative to working directory)")
                }

                Controls.TextField {
                    id: workDirectory
                    Kirigami.FormData.label: qsTr("Working directory:")
                    Layout.fillWidth: true
                    text: backend.defaultWorkingDirectory
                    placeholderText: qsTr("/path/to/game/directory")
                }

                Controls.TextField {
                    id: layerDirectory
                    Kirigami.FormData.label: qsTr("Vulkan layer manifest directory:")
                    Layout.fillWidth: true
                    text: backend.defaultLayerDirectory
                    placeholderText: qsTr(".../share/vulkan/explicit_layer.d")
                }

                Controls.TextField {
                    id: capturePath
                    Kirigami.FormData.label: qsTr("Capture JSONL:")
                    Layout.fillWidth: true
                    text: backend.defaultCapturePath
                }

                Controls.TextField {
                    id: cachePath
                    Kirigami.FormData.label: qsTr("Warmed cache output:")
                    Layout.fillWidth: true
                    text: backend.defaultCachePath
                }

                Controls.TextArea {
                    id: extraArgumentsField
                    Kirigami.FormData.label: qsTr("Extra arguments:")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    placeholderText: qsTr("Optional — one argument per line")
                    wrapMode: TextEdit.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Capture native")
                    icon.name: "media-record"
                    onClicked: captureNative()
                }

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Capture via Proton")
                    icon.name: "media-record"
                    onClicked: captureProton()
                }
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Inspect and warm")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Inspect capture")
                    icon.name: "document-properties"
                    onClicked: inspectCapture()
                }

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Warm cache")
                    icon.name: "run-build"
                    onClicked: warmCapture()
                }
            }

            Controls.CheckBox {
                id: requireComplete
                text: qsTr("Require complete replay (fail if any compute/graphics pipeline is skipped or fails)")
                checked: true
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: qsTr("Launch with the warmed cache")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Launch native")
                    icon.name: "media-playback-start"
                    onClicked: launchNative()
                }

                Controls.Button {
                    Layout.fillWidth: true
                    enabled: !backend.busy
                    text: qsTr("Launch via Proton")
                    icon.name: "media-playback-start"
                    onClicked: launchProton()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Controls.Label {
                    Layout.fillWidth: true
                    text: backend.busy
                        ? qsTr("Running SCSKiller command…")
                        : qsTr("Last command exit code: %1").arg(backend.exitCode)
                }

                Controls.Button {
                    enabled: !backend.busy
                    text: qsTr("Clear output")
                    onClicked: backend.clearOutput()
                }
            }

            Controls.TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                text: backend.output
                placeholderText: qsTr("Command output and diagnostics will appear here.")
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("CLI launcher: %1").arg(backend.launcherPath.length > 0
                                                   ? backend.launcherPath
                                                   : qsTr("not found — set SCSKILLER_CLI or add scskiller-linux to PATH"))
            }
        }
    }
}
