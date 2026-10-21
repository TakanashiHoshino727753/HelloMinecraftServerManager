import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MinecraftServerManager

// 远控端配对 / 连接页（同桌面风格）。首次或断连时弹出，要求先配对才能传输数据。
// 配对流程：在本地端「控制器设置 → 远控端配对」生成配对码/二维码（hmcsm-remote://CODE@host:port?s=1），
// 本页粘贴 URI 或手动输入即可换取长期 deviceToken（即便本地端重置访问令牌也能直接重连）。
// 采用无边框 + 自定义 TitleBar，使全局「标题栏」设置对所有窗口一致生效。
Window {
    id: dlg
    width: 520; height: 620
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    modality: Qt.ApplicationModal

    function tr(s) { return (typeof I18n !== 'undefined') ? I18n.t(s, I18n.lang) : s }

    Connections {
        target: remote
        function onConnectedChanged() { if (remote.connected) dlg.hide() }
    }

    // 圆角窗口主体
    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.bg
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TitleBar {
                id: titleBar
                window: dlg
                showDownloads: false
                showMin: false
                showMax: false
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 22
                spacing: 14

                Label {
                    text: tr("远控端配对")
                    font.bold: true; font.pixelSize: 19; color: Theme.text
                }
                Label {
                    text: tr("配对后远控端才能读取/控制本地端服务器。本地端重置访问令牌不影响已配对设备。")
                    wrapMode: Text.Wrap; color: Theme.textMuted; font.pixelSize: 12
                    Layout.fillWidth: true
                }

                // 连接状态条
                Rectangle {
                    Layout.fillWidth: true; height: 40; radius: Theme.radius
                    color: remote.connected ? Theme.success : (remote.pairing ? Theme.accentSoft : Theme.panelAlt)
                    border.color: Theme.border
                    Label {
                        anchors.centerIn: parent
                        text: remote.connected ? tr("已连接 · %1").arg(remote.host + ":" + remote.port)
                                             : (remote.paired ? tr("已配对 · %1").arg(remote.deviceName)
                                                              : (remote.pairing ? tr("配对中…") : tr("未连接")))
                        color: remote.connected ? "#ffffff" : Theme.text
                        font.pixelSize: 13
                    }
                }

                // —— 方式一：扫码 / 粘贴 URI（推荐）——
                GroupBox {
                    Layout.fillWidth: true
                    title: tr("方式一 · 扫码 / 粘贴配对串")
                    font.pixelSize: 12
                    palette.text: Theme.textMuted
                    ColumnLayout { spacing: 10; width: parent.width
                        TextField {
                            id: uriField
                            Layout.fillWidth: true; height: Theme.controlHeight
                            placeholderText: "hmcsm-remote://CODE@host:port"
                            color: Theme.text
                            background: Rectangle { radius: 6; color: Theme.panel; border.color: Theme.border }
                        }
                        TextField {
                            id: nameField
                            Layout.fillWidth: true; height: Theme.controlHeight
                            placeholderText: tr("本机名称（如 客厅电脑）")
                            text: remote.deviceName
                            color: Theme.text
                            background: Rectangle { radius: 6; color: Theme.panel; border.color: Theme.border }
                        }
                        AccentButton {
                            text: tr("配对并连接")
                            enabled: uriField.text.trim() !== ""
                            onClicked: remote.pairWithUri(uriField.text, nameField.text || tr("远控端"))
                        }
                    }
                }

                // —— 方式二：手动输入 ——
                GroupBox {
                    Layout.fillWidth: true
                    title: tr("方式二 · 手动输入")
                    font.pixelSize: 12
                    palette.text: Theme.textMuted
                    ColumnLayout { spacing: 10; width: parent.width
                        RowLayout { spacing: 8
                            TextField {
                                id: hostField
                                Layout.fillWidth: true; height: Theme.controlHeight
                                placeholderText: "host"; text: "127.0.0.1"
                                color: Theme.text
                                background: Rectangle { radius: 6; color: Theme.panel; border.color: Theme.border }
                            }
                            SpinBox {
                                id: portField
                                from: 1; to: 65535; value: 25577; height: Theme.controlHeight
                                editable: true
                            }
                            CheckBox { id: httpsBox; text: "HTTPS"; palette.text: Theme.text }
                        }
                        RowLayout { spacing: 8
                            AccentButton { text: tr("获取配对码"); onClicked: remote.startPair(hostField.text, portField.value, httpsBox.checked) }
                            Label { text: remote.pairCode ? (tr("当前码：") + remote.pairCode) : ""; color: Theme.accent; font.bold: true }
                        }
                        TextField {
                            id: codeField
                            Layout.fillWidth: true; height: Theme.controlHeight
                            placeholderText: tr("本地端显示的 6 位配对码")
                            color: Theme.text
                            background: Rectangle { radius: 6; color: Theme.panel; border.color: Theme.border }
                        }
                        AccentButton {
                            text: tr("用此码配对")
                            enabled: codeField.text.trim() !== ""
                            onClicked: remote.finishPair(codeField.text, hostField.text, portField.value, httpsBox.checked, nameField.text || tr("远控端"))
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    text: remote.lastError ? (tr("错误：") + remote.lastError) : ""
                    color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap
                }

                AccentButton {
                    text: tr("忘记配对并断开")
                    accentColor: Theme.danger
                    enabled: remote.paired
                    Layout.alignment: Qt.AlignRight
                    onClicked: remote.forgetPair()
                }
            }
        }
    }
}
