//
//  OnboardingView.swift
//  CrossFFB
//

import SwiftUI
import AppKit

/// Four things stand between a fresh install and force feedback. Rather than a
/// slideshow shown once, this is a list that reads real state and fills itself
/// in, so it is still worth opening when something stops working.
struct OnboardingView: View {
    @ObservedObject var bridgeManager: BridgeManager = .shared
    @ObservedObject var proxyInstaller: ProxyInstaller = .shared

    @Environment(\.colorScheme) private var colorScheme

    private var theme: PanelTheme {
        PanelTheme.forScheme(colorScheme)
    }

    private let signature = "Made by Matteo Seminara & Maurizio Seminara"
    private let wineOverride = "dinput8 = native,builtin"

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 6) {
                Text("Four things and you are driving")
                    .font(.system(size: 19, weight: .semibold))
                    .foregroundStyle(theme.numeral)

                Text("This list fills itself in as you go. Come back to it whenever something stops working.")
                    .font(.system(size: 13))
                    .foregroundStyle(theme.label)
            }
            .padding(.horizontal, 30)
            .padding(.top, 26)

            VStack(spacing: 8) {
                wheelRow
                folderRow
                proxyRow
                overrideRow
            }
            .padding(.horizontal, 30)
            .padding(.top, 20)

            Spacer(minLength: 24)

            HStack(spacing: 10) {
                Text(signature)
                    .font(.system(size: 11))
                    .foregroundStyle(theme.dim)

                Spacer()

                Button("Close") {
                    NSApp.keyWindow?.close()
                }
                .buttonStyle(InstrumentButtonStyle(theme: theme, isProminent: false))
            }
            .padding(.horizontal, 30)
            .padding(.bottom, 20)
        }
        .frame(minWidth: 700, minHeight: 420)
        .background(theme.panel)
        .onAppear {
            proxyInstaller.refreshStatus()

            // Seen once is enough for it to stop opening by itself; it stays
            // reachable from Help for when something breaks.
            OnboardingState.markCompleted()
        }
    }

    private var wheelRow: some View {
        ChecklistRow(
            title: "Wheel connected",
            detail: bridgeManager.isWheelConnected
                ? "Logitech G29 on USB"
                : "Plug the G29 in; CrossFFB starts its bridge on its own",
            state: bridgeManager.isWheelConnected ? .done : .todo,
            theme: theme,
            action: nil
        )
    }

    private var folderRow: some View {
        let chosen = proxyInstaller.gameFolderURL

        return ChecklistRow(
            title: "Game folder chosen",
            detail: chosen?.path ?? "Not chosen yet",
            state: chosen == nil ? .todo : .done,
            theme: theme,
            action: .init(
                title: chosen == nil ? "Choose" : "Change",
                isProminent: chosen == nil
            ) {
                proxyInstaller.chooseGameFolder()
            },
            detailFont: chosen == nil
                ? .system(size: 11)
                : .system(size: 10.5, design: .monospaced)
        )
    }

    private var proxyRow: some View {
        ChecklistRow(
            title: "Proxy installed",
            detail: proxyInstaller.isProxyInstalled
                ? proxyInstaller.proxyStatusText
                : "dinput8.dll goes beside the game executable",
            state: proxyInstaller.isProxyInstalled ? .done : .todo,
            theme: theme,
            action: proxyInstaller.isProxyInstalled
                ? nil
                : .init(title: "Install", isProminent: proxyInstaller.canInstallProxy) {
                    if proxyInstaller.canInstallProxy {
                        proxyInstaller.installProxy()
                    } else {
                        SetupWindowController.shared.show()
                    }
                }
        )
    }

    /// CrossFFB cannot set the bottle override yet, and cannot read it either -
    /// but a game that reaches the bridge proves it took. Patching the bottle
    /// safely is on the list; until then this row hands over the line to paste.
    private var overrideRow: some View {
        ChecklistRow(
            title: "Wine override set",
            detail: bridgeManager.hasSeenGameConnect
                ? "Confirmed: a game has reached the bridge"
                : "\(wineOverride) - CrossFFB cannot set this for you yet",
            state: bridgeManager.hasSeenGameConnect ? .done : .attention,
            theme: theme,
            action: bridgeManager.hasSeenGameConnect
                ? nil
                : .init(title: "Copy", isProminent: false) {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(wineOverride, forType: .string)
                }
        )
    }
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
