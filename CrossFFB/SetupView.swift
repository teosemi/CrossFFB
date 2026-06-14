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
