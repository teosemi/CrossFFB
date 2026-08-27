//
//  MenuBarView.swift
//  CrossFFB
//

import SwiftUI
import AppKit

struct MenuBarView: View {
    @ObservedObject var bridgeManager: BridgeManager
    @Environment(\.colorScheme) private var colorScheme

    private var theme: PanelTheme {
        PanelTheme.forScheme(colorScheme)
    }

    private let rangePresets = [540, 720, 900]

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            lampRow
            rangeGauge
            presetRow
            forceControl
            damperControl

            Rectangle()
                .fill(theme.rule)
                .frame(height: 1)

            footerRow
        }
        .padding(16)
        .frame(width: 300)
        .background(theme.panel)
        .onAppear {
            bridgeManager.startIfNeeded()

            if bridgeManager.isLogVisible {
                LogHUDWindowController.shared.show(bridgeManager: bridgeManager)
            }
        }
    }

    private var lampRow: some View {
        HStack(spacing: 12) {
            StatusLamp(title: "WHEEL", isOn: bridgeManager.isWheelConnected, theme: theme)
            StatusLamp(title: "GAME", isOn: bridgeManager.isGameConnected, theme: theme)

            Spacer()

            Button {
                SetupWindowController.shared.show()
            } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 13))
                    .foregroundStyle(theme.dim)
            }
            .buttonStyle(.plain)
            .help("Open CrossFFB Setup")
            .keyboardShortcut(",", modifiers: [.command])
        }
    }

    private var rangeGauge: some View {
        ArcGauge(
            value: bridgeManager.rangeDegrees,
            bounds: 40...900,
            theme: theme,
            onScrub: { bridgeManager.setRange($0) }
        ) {
            VStack(spacing: 0) {
                EditableValue(
                    display: "\(Int(bridgeManager.rangeDegrees.rounded()))",
                    editSeed: "\(Int(bridgeManager.rangeDegrees.rounded()))",
                    font: .condensed(46),
                    foreground: theme.numeral,
                    accent: theme.accent,
                    fieldWidth: 84,
                    onCommit: { bridgeManager.setRange(min(max($0, 40), 900)) }
                )

                Text("DEGREES")
                    .font(.condensed(11, weight: .medium))
                    .tracking(3)
                    .foregroundStyle(theme.dim)
            }
        }
    }

    private var presetRow: some View {
        PresetChips(
            presets: rangePresets,
            selected: rangePresets.first { Double($0) == bridgeManager.rangeDegrees.rounded() },
            theme: theme,
            onSelect: { bridgeManager.applyRangePreset(Double($0)) }
        )
    }

    private var forceControl: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 9) {
                Image(systemName: "waveform.path")
                    .font(.system(size: 13))
                    .foregroundStyle(theme.icon)

                Text("FORCE")
                    .font(.condensed(14, weight: .medium))
                    .tracking(1.4)
                    .foregroundStyle(theme.label)

                Spacer()

                EditableValue(
                    display: String(format: "%.2f", bridgeManager.gain),
                    editSeed: String(format: "%.2f", bridgeManager.gain),
                    font: .condensed(26),
                    foreground: theme.numeral,
                    accent: theme.accent,
                    fieldWidth: 62,
                    onCommit: { bridgeManager.setGain(min(max($0, 0), 1.5)) }
                )
            }

            StepScale(
                fraction: bridgeManager.gain / 1.5,
                steps: 16,
                theme: theme,
                onScrub: { bridgeManager.setGain($0 * 1.5) }
            )
        }
    }

    private var damperControl: some View {
        ThickSlider(
            title: "DAMPER",
            fraction: bridgeManager.damperGain,
            systemImage: "drop.fill",
            theme: theme,
            onScrub: { bridgeManager.setDamperGain($0) }
        ) {
            EditableValue(
                display: String(format: "%.2f", bridgeManager.damperGain),
                editSeed: String(format: "%.2f", bridgeManager.damperGain),
                font: .condensed(24),
                foreground: theme.numeral,
                accent: theme.accent,
                fieldWidth: 58,
                onCommit: { bridgeManager.setDamperGain(min(max($0, 0), 1)) }
            )
        }
    }

    private var footerRow: some View {
        HStack(spacing: 14) {
            footerButton("RESET") {
                bridgeManager.resetWheel()
            }
            .disabled(!bridgeManager.isRunning)

            footerButton(bridgeManager.isLogVisible ? "HIDE LOG" : "LOG") {
                bridgeManager.toggleLogVisibility()
                LogHUDWindowController.shared.setVisible(
                    bridgeManager.isLogVisible,
                    bridgeManager: bridgeManager
                )
            }

            Spacer()

            if bridgeManager.isRunning {
                footerButton("STOP", tint: theme.accentText) {
                    bridgeManager.stop()
                }
            } else {
                footerButton("START", tint: theme.accentText) {
                    bridgeManager.start()
                }
            }

            footerButton("QUIT") {
                LogHUDWindowController.shared.close()
                bridgeManager.stopForAppTermination()
                NSApplication.shared.terminate(nil)
            }
        }
    }

    private func footerButton(
        _ title: String,
        tint: Color? = nil,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Text(title)
                .font(.condensed(13, weight: tint == nil ? .medium : .semibold))
                .tracking(1.3)
                .foregroundStyle(tint ?? theme.label)
        }
        .buttonStyle(.plain)
    }
}
