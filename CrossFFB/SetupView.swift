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

                    Text("Configure the Windows proxy for a game folder.")
                        .foregroundStyle(.secondary)
                }
            }

            Divider()

            VStack(alignment: .leading, spacing: 10) {
                Text("Windows Proxy")
                    .font(.headline)

                Text("Choose the folder that contains the game executable. CrossFFB installs dinput8.dll locally in that folder, without changing the whole bottle.")
                    .fixedSize(horizontal: false, vertical: true)
                    .foregroundStyle(.secondary)
            }

            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text("Game EXE Folder")
                        .font(.headline)

                    Spacer()

                    Button("Choose Folder...") {
                        proxyInstaller.chooseGameFolder()
                    }
                }

                Text(proxyInstaller.gameFolderDisplayText)
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
                Text("Status")
                    .font(.headline)

                Label(proxyInstaller.exeStatusText, systemImage: proxyInstaller.exeStatusIcon)
                    .foregroundStyle(proxyInstaller.exeStatusColor)

                Label(proxyInstaller.proxyStatusText, systemImage: proxyInstaller.proxyStatusIcon)
                    .foregroundStyle(proxyInstaller.proxyStatusColor)

                if !proxyInstaller.detailText.isEmpty {
                    Text(proxyInstaller.detailText)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(8)
                        .background(Color(nsColor: .textBackgroundColor))
                        .clipShape(RoundedRectangle(cornerRadius: 6))
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
                .frame(height: 90)
                .background(Color(nsColor: .textBackgroundColor))
                .clipShape(RoundedRectangle(cornerRadius: 6))
            }

            Spacer()

            HStack {
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

                Spacer()

                Button("Close") {
                    NSApp.keyWindow?.close()
                }
                .keyboardShortcut(.cancelAction)
            }
        }
        .padding(24)
        .frame(minWidth: 780, minHeight: 650)
        .onAppear {
            proxyInstaller.refreshStatus()
        }
    }
}
