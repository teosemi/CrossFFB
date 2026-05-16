//
//  MenuBarView.swift
//  CrossFFB
//
//  Created by teo on 16/05/2026.
//

import SwiftUI
import AppKit

struct MenuBarView: View {
    @ObservedObject var bridgeManager: BridgeManager

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            headerView

            Divider()

            controlsCard

            Divider()

            actionsCard

            Divider()

            logSection

            Divider()

            footerActions
        }
        .padding(14)
        .frame(width: bridgeManager.isLogVisible ? 470 : 310)
        .animation(.easeInOut(duration: 0.22), value: bridgeManager.isLogVisible)
        .onAppear {
            bridgeManager.startIfNeeded()
        }
    }

    private var headerView: some View {
        HStack(spacing: 10) {
            Image(systemName: bridgeManager.isRunning ? "steeringwheel.circle.fill" : "steeringwheel.circle")
                .font(.system(size: 30))
                .symbolRenderingMode(.hierarchical)

            VStack(alignment: .leading, spacing: 3) {
                Text("CrossFFB")
                    .font(.headline)

                HStack(spacing: 6) {
                    Circle()
                        .frame(width: 7, height: 7)
                        .foregroundStyle(statusColor)

                    Text(bridgeManager.statusText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }

            Spacer()

            Button {
                SetupWindowController.shared.show()
            } label: {
                Label("Setup", systemImage: "gearshape")
                    .labelStyle(.iconOnly)
            }
            .help("Open CrossFFB Setup")
            .keyboardShortcut(",", modifiers: [.command])
        }
    }

    private var controlsCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            gainControl
            rangeControl
        }
        .padding(10)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private var gainControl: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label("FFB Gain", systemImage: "waveform.path")
                    .font(.subheadline)

                Spacer()

                Text(String(format: "%.2f", bridgeManager.gain))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
            }

            Slider(
                value: Binding(
                    get: { bridgeManager.gain },
                    set: { bridgeManager.setGain($0) }
                ),
                in: 0.0...1.5,
                step: 0.05
            )
        }
    }

    private var rangeControl: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("Steering Range", systemImage: "gauge")
                    .font(.subheadline)

                Spacer()

                Text("\(Int(bridgeManager.rangeDegrees.rounded()))°")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
            }

            Slider(
                value: Binding(
                    get: { bridgeManager.rangeDegrees },
                    set: { bridgeManager.setRange($0) }
                ),
                in: 40...900,
                step: 10
            )

            HStack(spacing: 8) {
                Button("540°") {
                    bridgeManager.applyRangePreset(540)
                }

                Button("720°") {
                    bridgeManager.applyRangePreset(720)
                }

                Button("900°") {
                    bridgeManager.applyRangePreset(900)
                }
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
        }
    }

    private var actionsCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                if bridgeManager.isRunning {
                    Button {
                        bridgeManager.resetWheel()
                    } label: {
                        Label("Reset Wheel", systemImage: "arrow.counterclockwise")
                    }

                    Button {
                        bridgeManager.stop()
                    } label: {
                        Label("Stop", systemImage: "stop.fill")
                    }
                } else {
                    Button {
                        bridgeManager.start()
                    } label: {
                        Label("Start", systemImage: "play.fill")
                    }
                }
            }
            .buttonStyle(.bordered)
        }
    }

    private var logSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Button {
                    withAnimation(.easeInOut(duration: 0.22)) {
                        bridgeManager.toggleLogVisibility()
                    }
                } label: {
                    Label(
                        bridgeManager.isLogVisible ? "Hide Log" : "Show Log",
                        systemImage: bridgeManager.isLogVisible ? "chevron.up" : "chevron.down"
                    )
                }
                .buttonStyle(.plain)

                Spacer()

                if bridgeManager.isLogVisible {
                    Button("Clear") {
                        bridgeManager.clearLog()
                    }
                    .font(.caption)
                    .transition(.opacity)
                }
            }

            if bridgeManager.isLogVisible {
                ScrollViewReader { proxy in
                    ScrollView {
                        Text(bridgeManager.logText.isEmpty ? "No log output yet." : bridgeManager.logText)
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(bridgeManager.logText.isEmpty ? .secondary : .primary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .textSelection(.enabled)
                            .id("log-bottom")
                            .padding(8)
                    }
                    .frame(height: 170)
                    .background(Color(nsColor: .textBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .transition(
                        .asymmetric(
                            insertion: .opacity.combined(with: .move(edge: .top)),
                            removal: .opacity.combined(with: .move(edge: .top))
                        )
                    )
                    .onChange(of: bridgeManager.logText) { _, _ in
                        proxy.scrollTo("log-bottom", anchor: .bottom)
                    }
                }
            }
        }
    }

    private var footerActions: some View {
        HStack {
            Spacer()

            Button {
                bridgeManager.stopForAppTermination()
                NSApplication.shared.terminate(nil)
            } label: {
                Label("Quit", systemImage: "power")
            }
            .buttonStyle(.plain)
        }
        .foregroundStyle(.secondary)
    }

    private var statusColor: Color {
        switch bridgeManager.statusText {
        case "Running", "Wheel connected", "Game connected", "Gain updated", "Range updated", "Wheel reset":
            return .green

        case "Starting...", "Stopping...":
            return .orange

        case "Wheel not found", "Bridge file missing", "Bridge not executable":
            return .red

        default:
            return bridgeManager.isRunning ? .green : .secondary
        }
    }
}
