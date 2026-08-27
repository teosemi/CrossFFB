//
//  SetupView.swift
//  CrossFFB
//
//  Created by teo on 16/05/2026.
//

import SwiftUI
import AppKit

struct SetupView: View {
    @StateObject private var proxyInstaller = ProxyInstaller.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                Image(systemName: "steeringwheel")
                    .font(.system(size: 32))

                VStack(alignment: .leading, spacing: 4) {
                    Text("CrossFFB Setup")
                        .font(.title2)
                        .bold()

                    Text("Install the Windows proxy in a game folder.")
                        .foregroundStyle(.secondary)
                }

                Spacer()

                Button {
                    OnboardingWindowController.shared.show()
                } label: {
                    Label("Help", systemImage: "questionmark.circle")
                }
            }

            Divider()

            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text("Game Folder")
                        .font(.headline)

                    Spacer()

                    Button("Choose Folder...") {
                        proxyInstaller.chooseGameFolder()
                    }
                }

                Text(proxyInstaller.folderText)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .lineLimit(3)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
                    .background(Color(nsColor: .textBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 6))
            }

            VStack(alignment: .leading, spacing: 8) {
                Text("Executable")
                    .font(.headline)

                Label(proxyInstaller.exeText, systemImage: proxyInstaller.exeStatusIcon)
                    .foregroundStyle(proxyInstaller.exeStatusColor)

                if !proxyInstaller.suggestedFolderText.isEmpty {
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        Label(proxyInstaller.suggestedFolderText, systemImage: "arrow.turn.down.right")
                            .font(.caption)
                            .foregroundStyle(.orange)

                        Button("Use That Folder") {
                            proxyInstaller.useSuggestedFolder()
                        }
                        .controlSize(.small)
                    }
                }
            }

            VStack(alignment: .leading, spacing: 8) {
                Text("Proxy")
                    .font(.headline)

                Label(proxyInstaller.proxyStatusText, systemImage: proxyInstaller.proxyStatusIcon)
                    .foregroundStyle(proxyInstaller.proxyStatusColor)

                if !proxyInstaller.lastActionText.isEmpty {
                    Text(proxyInstaller.lastActionText)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Divider()

                Toggle("Detailed proxy log", isOn: Binding(
                    get: { proxyInstaller.isVerboseLogEnabled },
                    set: { proxyInstaller.setVerboseLog($0) }
                ))
                .disabled(proxyInstaller.gameFolderURL == nil)

                Text(proxyInstaller.isVerboseLogEnabled
                     ? "Logs every force feedback event, which grows by tens of megabytes per session. Takes effect when the game restarts. \(proxyInstaller.verboseLogDetailText)"
                     : "Only startup and errors are logged. Turn this on to diagnose a problem, then turn it back off. \(proxyInstaller.verboseLogDetailText)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("Install History")
                        .font(.headline)

                    Spacer()

                    Button("Clear History") {
                        proxyInstaller.clearHistory()
                    }
                }

                ScrollView {
                    Text(proxyInstaller.historyDisplayText)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(8)
                }
                .frame(height: 100)
                .background(Color(nsColor: .textBackgroundColor))
                .clipShape(RoundedRectangle(cornerRadius: 6))
            }

            Spacer()

            Divider()

            HStack {
                Text("Made by Matteo Seminara & Maurizio Seminara")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Spacer()

                Button("Install Proxy") {
                    proxyInstaller.installProxy()
                }
                .disabled(!proxyInstaller.canInstallProxy)

                Button("Remove Proxy") {
                    proxyInstaller.removeProxy()
                }
                .disabled(!proxyInstaller.canRemoveProxy)

                Button("Refresh") {
                    proxyInstaller.refreshStatus()
                }
                .disabled(proxyInstaller.gameFolderURL == nil)

                Button("Reveal Folder") {
                    proxyInstaller.revealGameFolder()
                }
                .disabled(proxyInstaller.gameFolderURL == nil)

                Button("Close") {
                    NSApp.keyWindow?.close()
                }
                .keyboardShortcut(.cancelAction)
            }
        }
        .padding(24)
        .frame(minWidth: 760, minHeight: 580)
        .onAppear {
            proxyInstaller.refreshStatus()
        }
    }
}
