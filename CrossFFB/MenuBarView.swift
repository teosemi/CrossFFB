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
            Text("CrossFFB")
                .font(.headline)

            Divider()

            HStack {
                Circle()
                    .frame(width: 8, height: 8)
                    .foregroundStyle(bridgeManager.isRunning ? .green : .secondary)

                Text(bridgeManager.statusText)
                    .font(.caption)
            }

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("FFB Gain")
                    Spacer()
                    Text(String(format: "%.2f", bridgeManager.gain))
                        .monospacedDigit()
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

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Steering Range")
                    Spacer()
                    Text("\(Int(bridgeManager.rangeDegrees.rounded()))°")
                        .monospacedDigit()
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
            }

            Divider()

            Button("Open Setup...") {
                SetupWindowController.shared.show()
            }

            Divider()

            if bridgeManager.isRunning {
                Button("Reset Wheel") {
                    bridgeManager.resetWheel()
                }

                Button("Stop Bridge") {
                    bridgeManager.stop()
                }
            } else {
                Button("Start Bridge") {
                    bridgeManager.start()
                }
            }

            Divider()

            Button(bridgeManager.isLogVisible ? "Hide Log" : "Show Log") {
                bridgeManager.toggleLogVisibility()
            }

            if bridgeManager.isLogVisible {
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("Bridge Log")
                            .font(.caption)
                            .foregroundStyle(.secondary)

                        Spacer()

                        Button("Clear") {
                            bridgeManager.clearLog()
                        }
                        .font(.caption)
                    }

                    ScrollViewReader { proxy in
                        ScrollView {
                            Text(bridgeManager.logText)
                                .font(.system(size: 10, design: .monospaced))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .textSelection(.enabled)
                                .id("log-bottom")
                        }
                        .frame(height: 160)
                        .background(Color(nsColor: .textBackgroundColor))
                        .clipShape(RoundedRectangle(cornerRadius: 6))
                        .onChange(of: bridgeManager.logText) { _, _ in
                            proxy.scrollTo("log-bottom", anchor: .bottom)
                        }
                    }
                }
            }

            Divider()

            Button("Quit") {
                bridgeManager.stopForAppTermination()
                NSApplication.shared.terminate(nil)
            }
        }
        .padding()
        .frame(width: bridgeManager.isLogVisible ? 460 : 280)
        .onAppear {
            bridgeManager.startIfNeeded()
        }
    }
}
