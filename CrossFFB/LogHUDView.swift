//
//  LogHUDView.swift
//  CrossFFB
//

import SwiftUI

struct LogHUDView: View {
    @ObservedObject var bridgeManager: BridgeManager
    @Environment(\.colorScheme) private var colorScheme

    let onClose: () -> Void

    private var theme: PanelTheme {
        PanelTheme.forScheme(colorScheme)
    }

    private var tail: [String] {
        bridgeManager.logText
            .split(separator: "\n", omittingEmptySubsequences: false)
            .suffix(12)
            .map(String.init)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                StatusLamp(title: "WHEEL", isOn: bridgeManager.isWheelConnected, theme: theme)
                StatusLamp(title: "GAME", isOn: bridgeManager.isGameConnected, theme: theme)

                Spacer()

                Button {
                    bridgeManager.clearLog()
                } label: {
                    Text("CLEAR")
                        .font(.condensed(12, weight: .medium))
                        .tracking(1.2)
                        .foregroundStyle(theme.dim)
                }
                .buttonStyle(.plain)

                Button(action: onClose) {
                    Image(systemName: "xmark")
                        .font(.system(size: 9, weight: .bold))
                        .foregroundStyle(theme.dim)
                }
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 9)

            Rectangle()
                .fill(theme.rule)
                .frame(height: 1)

            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    if tail.isEmpty {
                        Text("No log output yet.")
                            .foregroundStyle(theme.dim)
                    } else {
                        ForEach(Array(tail.enumerated()), id: \.offset) { _, line in
                            Text(line)
                                .foregroundStyle(theme.label)
                                .textSelection(.enabled)
                        }
                    }
                }
                .font(.system(size: 9.5, design: .monospaced))
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 12)
                .padding(.vertical, 10)
            }
        }
        .frame(width: 306, height: 230, alignment: .topLeading)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(theme.panelBorder, lineWidth: 1)
        )
    }
}
