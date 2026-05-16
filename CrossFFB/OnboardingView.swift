//
//  OnboardingView.swift
//  CrossFFB
//
//  Created by teo on 17/05/2026.
//

import SwiftUI
import AppKit

struct OnboardingView: View {
    @State private var stepIndex: Int = 0

    private let steps: [OnboardingStep] = [
        OnboardingStep(
            icon: "steeringwheel",
            title: "Welcome to CrossFFB",
            subtitle: "Force Feedback bridge for Logitech G29 on macOS.",
            body: """
            CrossFFB connects a Windows game running through CrossOver/Wine to your Logitech G29.

            The game sends Force Feedback to a local dinput8.dll proxy.
            CrossFFB receives those commands and applies them to the wheel through the macOS bridge.
            """,
            note: "Game → dinput8.dll proxy → CrossFFB bridge → Logitech G29"
        ),
        OnboardingStep(
            icon: "cable.connector",
            title: "Connect your wheel",
            subtitle: "Plug your Logitech G29 into USB before playing.",
            body: """
            Start CrossFFB with the wheel connected.

            When the wheel is detected, the menu bar status will show Running or Wheel connected.
            If the wheel is not connected, CrossFFB will show Wheel not found.
            """,
            note: "Keep your hands away from the wheel while the bridge starts."
        ),
        OnboardingStep(
            icon: "folder",
            title: "Choose the game folder",
            subtitle: "Select the folder that contains the Windows game executable.",
            body: """
            Open Setup and choose the folder where the game .exe is located.

            For Euro Truck Simulator 2, choose the win_x64 folder that contains eurotrucks2.exe.
            """,
            note: "Example: Euro Truck Simulator 2/bin/win_x64"
        ),
        OnboardingStep(
            icon: "square.and.arrow.down",
            title: "Install the proxy",
            subtitle: "CrossFFB installs dinput8.dll locally in the game folder.",
            body: """
            Press Install Proxy in Setup.

            CrossFFB copies dinput8.dll next to the game executable.
            It does not modify the whole CrossOver bottle.

            If another dinput8.dll already exists, CrossFFB keeps a backup before replacing it.
            """,
            note: "You can remove the proxy later with Remove Proxy."
        ),
        OnboardingStep(
            icon: "play.circle",
            title: "Start playing",
            subtitle: "Start CrossFFB first, then launch the game.",
            body: """
            When the game connects, CrossFFB will show Game connected.

            Use FFB Gain to adjust force strength.
            Use Steering Range to adjust wheel rotation degrees.
            """,
            note: "Recommended starting values: Gain 1.00, Range 900°."
        )
    ]

    var body: some View {
        let step = steps[stepIndex]

        VStack(alignment: .leading, spacing: 20) {
            HStack(spacing: 14) {
                Image(systemName: step.icon)
                    .font(.system(size: 34))
                    .frame(width: 44)

                VStack(alignment: .leading, spacing: 4) {
                    Text(step.title)
                        .font(.title2)
                        .bold()

                    Text(step.subtitle)
                        .foregroundStyle(.secondary)
                }
            }

            ProgressView(value: Double(stepIndex + 1), total: Double(steps.count))

            Text(step.body)
                .font(.body)
                .fixedSize(horizontal: false, vertical: true)

            if !step.note.isEmpty {
                Text(step.note)
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(10)
                    .background(Color(nsColor: .textBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
            }

            Spacer()

            HStack {
                Text("Step \(stepIndex + 1) of \(steps.count)")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Spacer()

                Button("Back") {
                    stepIndex = max(0, stepIndex - 1)
                }
                .disabled(stepIndex == 0)

                if stepIndex < steps.count - 1 {
                    Button("Next") {
                        stepIndex += 1
                    }
                    .keyboardShortcut(.defaultAction)
                } else {
                    Button("Finish") {
                        OnboardingState.markCompleted()
                        NSApp.keyWindow?.close()
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
        }
        .padding(28)
        .frame(minWidth: 680, minHeight: 480)
    }
}

private struct OnboardingStep {
    let icon: String
    let title: String
    let subtitle: String
    let body: String
    let note: String
}

enum OnboardingState {
    private static let hasCompletedKey = "crossffb.onboarding.hasCompleted"

    static var hasCompleted: Bool {
        UserDefaults.standard.bool(forKey: hasCompletedKey)
    }

    static func markCompleted() {
        UserDefaults.standard.set(true, forKey: hasCompletedKey)
    }

    static func reset() {
        UserDefaults.standard.set(false, forKey: hasCompletedKey)
    }
}
