//
//  MenuBarView.swift
//  CrossFFB
//

import SwiftUI
import AppKit

struct MenuBarView: View {
    @ObservedObject var bridgeManager: BridgeManager
    @Environment(\.colorScheme) private var colorScheme

    @State private var isEditingRange = false
    @State private var rangeDraft = ""
    @FocusState private var isRangeFieldFocused: Bool

    private var theme: PanelTheme {
        PanelTheme.forScheme(colorScheme)
    }

    private let rangePresets = [540, 720, 900]

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            lampRow
            ArcGauge(value: bridgeManager.rangeDegrees, bounds: 40...900, theme: theme)
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

    @ViewBuilder
    private var presetRow: some View {
        if isEditingRange {
            rangeEditor
        } else {
            PresetChips(
                presets: rangePresets,
                selected: rangePresets.first { Double($0) == bridgeManager.rangeDegrees.rounded() },
                theme: theme,
                onSelect: { bridgeManager.applyRangePreset(Double($0)) },
                onCustom: {
                    rangeDraft = String(Int(bridgeManager.rangeDegrees.rounded()))
                    isEditingRange = true
                    isRangeFieldFocused = true
                }
            )
        }
    }

    /// The gauge is a poor way to enter an exact angle, so SET swaps the presets
    /// for a field. 40 to 900 is the range the bridge accepts.
    private var rangeEditor: some View {
        HStack(spacing: 8) {
            TextField("", text: $rangeDraft)
                .textFieldStyle(.plain)
                .font(.condensed(15))
                .foregroundStyle(theme.numeral)
                .focused($isRangeFieldFocused)
                .onSubmit(commitRange)
                .padding(.vertical, 4)
                .padding(.horizontal, 8)
                .background(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(theme.accent, lineWidth: 1)
                )

            Text("DEGREES")
                .font(.condensed(12, weight: .medium))
                .tracking(1.2)
                .foregroundStyle(theme.label)

            Spacer()

            Button(action: commitRange) {
                Text("DONE")
                    .font(.condensed(13, weight: .semibold))
                    .tracking(1.2)
                    .foregroundStyle(theme.accentText)
            }
            .buttonStyle(.plain)

            Button {
                isEditingRange = false
            } label: {
                Text("CANCEL")
                    .font(.condensed(13, weight: .medium))
                    .tracking(1.2)
                    .foregroundStyle(theme.label)
            }
            .buttonStyle(.plain)
        }
    }

    private func commitRange() {
        if let typed = Double(rangeDraft.trimmingCharacters(in: .whitespaces)) {
            bridgeManager.setRange(min(max(typed, 40), 900))
        }

        isEditingRange = false
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

                Text(String(format: "%.2f", bridgeManager.gain))
                    .font(.condensed(26))
                    .foregroundStyle(theme.numeral)
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
            value: String(format: "%.2f", bridgeManager.damperGain),
            fraction: bridgeManager.damperGain,
            systemImage: "drop.fill",
            theme: theme,
            onScrub: { bridgeManager.setDamperGain($0) }
        )
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
