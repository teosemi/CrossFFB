//
//  CrossFFBApp.swift
//  CrossFFB
//
//  Created by teo on 13/05/2026.
//

import SwiftUI

@main

struct CrossFFBApp: App {

    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    @StateObject private var bridgeManager = BridgeManager.shared

    var body: some Scene {

        MenuBarExtra {

            MenuBarView(bridgeManager: bridgeManager)

                .onAppear {

                    showOnboardingIfNeeded()

                }

        } label: {

            Image(systemName: bridgeManager.isRunning ? "steeringwheel.circle.fill" : "steeringwheel.circle")

        }

        .menuBarExtraStyle(.window)

        .commands {

            CommandGroup(replacing: .appSettings) {

                Button("Setup...") {

                    SetupWindowController.shared.show()

                }

                .keyboardShortcut(",", modifiers: [.command])

            }

            CommandGroup(replacing: .help) {
                Button("CrossFFB Onboarding") {
                    OnboardingWindowController.shared.show()
                }
                .keyboardShortcut("?", modifiers: [.command])
            }

        }

    }

    @MainActor

    private func showOnboardingIfNeeded() {

        guard !OnboardingState.hasCompleted else {

            return

        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {

            OnboardingWindowController.shared.show()

        }

    }

}
