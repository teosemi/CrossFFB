//
//  SetupView.swift
//  CrossFFB
//

import SwiftUI
import AppKit

struct SetupView: View {
    @ObservedObject var proxyInstaller: ProxyInstaller = .shared
    @Environment(\.colorScheme) private var colorScheme

    private var theme: PanelTheme {
        PanelTheme.forScheme(colorScheme)
    }

    var body: some View {
        VStack(spacing: 0) {
            header

            Divider().overlay(theme.rule)

            HStack(alignment: .top, spacing: 0) {
                leftColumn
                    .frame(maxWidth: .infinity, alignment: .leading)

                Divider().overlay(theme.rule)

                rightColumn
                    .frame(width: 260, alignment: .leading)
            }

            Spacer(minLength: 0)

            Divider().overlay(theme.rule)

            footer
        }
        .frame(minWidth: 680, minHeight: 460)
        .background(theme.panel)
        .onAppear {
            proxyInstaller.refreshStatus()
        }
    }

    private var header: some View {
        HStack(spacing: 10) {
            Text("Setup")
                .font(.system(size: 15, weight: .semibold))
                .foregroundStyle(theme.numeral)

            Spacer()

            Button("Help") {
                OnboardingWindowController.shared.show()
            }
            .buttonStyle(InstrumentButtonStyle(theme: theme))
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 16)
    }

    private var leftColumn: some View {
        VStack(alignment: .leading, spacing: 18) {
            section("GAME FOLDER") {
                Text(proxyInstaller.folderText)
                    .font(.system(size: 10.5, design: .monospaced))
                    .foregroundStyle(theme.body)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(12)
                    .background(
                        RoundedRectangle(cornerRadius: 9)
                            .fill(theme.surface)
                    )

                HStack(spacing: 8) {
                    Button("Choose Folder") {
                        proxyInstaller.chooseGameFolder()
                    }
                    .buttonStyle(InstrumentButtonStyle(theme: theme))

                    Button("Reveal") {
                        proxyInstaller.revealGameFolder()
                    }
                    .buttonStyle(InstrumentButtonStyle(theme: theme))
                    .disabled(proxyInstaller.gameFolderURL == nil)
                }
            }

            section("EXECUTABLE") {
                statusLine(
                    text: proxyInstaller.exeText,
                    colour: proxyInstaller.exeStatusColor
                )

                if !proxyInstaller.suggestedFolderText.isEmpty {
                    HStack(alignment: .firstTextBaseline, spacing: 10) {
                        Text(proxyInstaller.suggestedFolderText)
                            .font(.system(size: 11))
                            .foregroundStyle(theme.warning)
                            .fixedSize(horizontal: false, vertical: true)

                        Button("Use That Folder") {
                            proxyInstaller.useSuggestedFolder()
                        }
                        .buttonStyle(InstrumentButtonStyle(theme: theme))
                    }
                }
            }

            section("PROXY") {
                statusLine(
                    text: proxyInstaller.proxyStatusText,
                    colour: proxyInstaller.proxyStatusColor
                )

                if !proxyInstaller.lastActionText.isEmpty {
                    Text(proxyInstaller.lastActionText)
                        .font(.system(size: 11))
                        .foregroundStyle(theme.dim)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .padding(20)
    }

    private var rightColumn: some View {
        VStack(alignment: .leading, spacing: 18) {
            section("DETAILED PROXY LOG") {
                Toggle(isOn: Binding(
                    get: { proxyInstaller.isVerboseLogEnabled },
                    set: { proxyInstaller.setVerboseLog($0) }
                )) {
                    Text(proxyInstaller.isVerboseLogEnabled ? "On" : "Off")
                        .font(.system(size: 12.5))
                        .foregroundStyle(theme.body)
                }
                .toggleStyle(.switch)
                .tint(theme.accent)
                .disabled(proxyInstaller.gameFolderURL == nil)

                Text(verboseLogCaption)
                    .font(.system(size: 11))
                    .foregroundStyle(theme.dim)
                    .fixedSize(horizontal: false, vertical: true)
            }

            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    caption("INSTALL HISTORY")

                    Spacer()

                    Button("Clear") {
                        proxyInstaller.clearHistory()
                    }
                    .buttonStyle(.plain)
                    .font(.system(size: 11))
                    .foregroundStyle(theme.dim)
                }

                ScrollView {
                    Text(proxyInstaller.historyDisplayText)
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundStyle(theme.dim)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(maxHeight: 150)
            }
        }
        .padding(20)
    }

    private var verboseLogCaption: String {
        let state = proxyInstaller.isVerboseLogEnabled
            ? "Logs every force feedback event, which grows by tens of megabytes per session. Takes effect when the game restarts."
            : "Only startup and errors are logged. Turn this on to diagnose a problem, then turn it back off."

        return "\(state) \(proxyInstaller.verboseLogDetailText)"
    }

    private var footer: some View {
        HStack(spacing: 8) {
            Text("Made by Matteo Seminara & Maurizio Seminara")
                .font(.system(size: 11))
                .foregroundStyle(theme.dim)

            Spacer()

            Button("Remove") {
                proxyInstaller.removeProxy()
            }
            .buttonStyle(InstrumentButtonStyle(theme: theme))
            .disabled(!proxyInstaller.canRemoveProxy)

            Button("Refresh") {
                proxyInstaller.refreshStatus()
            }
            .buttonStyle(InstrumentButtonStyle(theme: theme))

            Button("Close") {
                NSApp.keyWindow?.close()
            }
            .buttonStyle(InstrumentButtonStyle(theme: theme))

            Button("Install Proxy") {
                proxyInstaller.installProxy()
            }
            .buttonStyle(InstrumentButtonStyle(theme: theme, isProminent: true))
            .disabled(!proxyInstaller.canInstallProxy)
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
    }

    @ViewBuilder
    private func section<Content: View>(
        _ title: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            caption(title)
            content()
        }
    }

    private func caption(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 12, weight: .semibold))
            .tracking(1.4)
            .foregroundStyle(theme.label)
    }

    private func statusLine(text: String, colour: Color) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 9) {
            Circle()
                .fill(colour)
                .frame(width: 7, height: 7)
                .offset(y: -1)

            Text(text)
                .font(.system(size: 12.5))
                .foregroundStyle(theme.body)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}
