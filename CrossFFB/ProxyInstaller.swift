//
//  ProxyInstaller.swift
//  CrossFFB
//
//  Created by teo on 17/05/2026.
//

import Foundation
import SwiftUI
import AppKit
import CryptoKit
import Combine

@MainActor
final class ProxyInstaller: ObservableObject {
    static let shared = ProxyInstaller()

    private enum DefaultsKey {
        static let gameFolderPath = "crossffb.proxy.gameFolderPath"
        static let installHistory = "crossffb.proxy.installHistory"
    }

    @Published var gameFolderURL: URL?

    @Published var folderText: String = "No folder selected"
    @Published var exeText: String = "No executable found"

    @Published var exeStatusIcon: String = "questionmark.circle"
    @Published var exeStatusColor: Color = .secondary

    @Published var proxyStatusText: String = "Proxy status unknown"
    @Published var proxyStatusIcon: String = "questionmark.circle"
    @Published var proxyStatusColor: Color = .secondary

    @Published var historyDisplayText: String = "No install history yet"
    @Published var lastActionText: String = ""

    private let proxyFileName = "dinput8.dll"
    private let backupFileName = "dinput8.dll.crossffb_backup"

    private var bundledProxyURL: URL? {
        Bundle.main.url(forResource: "dinput8", withExtension: "dll")
    }

    var canInstallProxy: Bool {
        guard let gameFolderURL, bundledProxyURL != nil else {
            return false
        }

        var isDirectory: ObjCBool = false
        return FileManager.default.fileExists(atPath: gameFolderURL.path, isDirectory: &isDirectory) && isDirectory.boolValue
    }

    var canRemoveProxy: Bool {
        guard let gameFolderURL else {
            return false
        }

        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        return FileManager.default.fileExists(atPath: targetProxyURL.path) ||
            FileManager.default.fileExists(atPath: backupURL.path)
    }

    private init() {
        if let savedPath = UserDefaults.standard.string(forKey: DefaultsKey.gameFolderPath), !savedPath.isEmpty {
            gameFolderURL = URL(fileURLWithPath: savedPath)
        }

        loadHistory()
        refreshStatus()
    }

    func chooseGameFolder() {
        let panel = NSOpenPanel()
        panel.title = "Choose Game EXE Folder"
        panel.message = "Choose the folder that contains the game executable, for example the folder with eurotrucks2.exe."
        panel.prompt = "Choose"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false

        if let gameFolderURL {
            panel.directoryURL = gameFolderURL
        }

        let response = panel.runModal()

        guard response == .OK, let selectedURL = panel.url else {
            return
        }

        gameFolderURL = selectedURL
        UserDefaults.standard.set(selectedURL.path, forKey: DefaultsKey.gameFolderPath)
        lastActionText = "Selected game folder."
        refreshStatus()
    }

    func refreshStatus() {
        guard let gameFolderURL else {
            folderText = "No folder selected"
            exeText = "No executable found"
            exeStatusIcon = "questionmark.circle"
            exeStatusColor = .secondary

            proxyStatusText = "Proxy status unknown"
            proxyStatusIcon = "questionmark.circle"
            proxyStatusColor = .secondary
            return
        }

        let fileManager = FileManager.default
        var isDirectory: ObjCBool = false

        guard fileManager.fileExists(atPath: gameFolderURL.path, isDirectory: &isDirectory), isDirectory.boolValue else {
            folderText = gameFolderURL.path
            exeText = "Selected folder does not exist"
            exeStatusIcon = "xmark.circle"
            exeStatusColor = .red

            proxyStatusText = "Proxy status unknown"
            proxyStatusIcon = "questionmark.circle"
            proxyStatusColor = .secondary
            return
        }

        folderText = gameFolderURL.path

        let exeFiles = findExeFiles(in: gameFolderURL)

        if exeFiles.isEmpty {
            exeText = "No .exe found"
            exeStatusIcon = "exclamationmark.triangle"
            exeStatusColor = .orange
        } else {
            exeText = exeFiles.map { $0.lastPathComponent }.joined(separator: ", ")
            exeStatusIcon = "checkmark.circle"
            exeStatusColor = .green
        }

        updateProxyStatus(gameFolderURL: gameFolderURL)
    }

    func revealGameFolder() {
        guard let gameFolderURL else {
            return
        }

        NSWorkspace.shared.activateFileViewerSelecting([gameFolderURL])
    }

    func installProxy() {
        guard let gameFolderURL else {
            lastActionText = "Choose a game folder first."
            refreshStatus()
            return
        }

        guard let bundledProxyURL else {
            lastActionText = "Bundled dinput8.dll not found."
            refreshStatus()
            return
        }

        let fileManager = FileManager.default
        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        do {
            if fileManager.fileExists(atPath: targetProxyURL.path) {
                if filesHaveSameSHA256(targetProxyURL, bundledProxyURL) {
                    lastActionText = "Proxy is already installed."
                    recordHistory(action: "Already installed", folderURL: gameFolderURL)
                    refreshStatus()
                    return
                }

                if !fileManager.fileExists(atPath: backupURL.path) {
                    try fileManager.copyItem(at: targetProxyURL, to: backupURL)
                    lastActionText = "Existing dinput8.dll backed up. Proxy installed."
                } else {
                    lastActionText = "Proxy installed. Existing backup kept."
                }

                try fileManager.removeItem(at: targetProxyURL)
            } else {
                lastActionText = "Proxy installed."
            }

            try fileManager.copyItem(at: bundledProxyURL, to: targetProxyURL)
            try? fileManager.setAttributes([.posixPermissions: 0o644], ofItemAtPath: targetProxyURL.path)

            recordHistory(action: "Installed", folderURL: gameFolderURL)
        } catch {
            lastActionText = "Install failed: \(error.localizedDescription)"
        }

        refreshStatus()
    }

    func removeProxy() {
        guard let gameFolderURL else {
            lastActionText = "Choose a game folder first."
            refreshStatus()
            return
        }

        let fileManager = FileManager.default
        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        do {
            let targetExists = fileManager.fileExists(atPath: targetProxyURL.path)
            let backupExists = fileManager.fileExists(atPath: backupURL.path)

            if backupExists {
                if targetExists {
                    try fileManager.removeItem(at: targetProxyURL)
                }

                try fileManager.moveItem(at: backupURL, to: targetProxyURL)
                lastActionText = "Proxy removed. Previous dinput8.dll restored."
                recordHistory(action: "Removed and restored backup", folderURL: gameFolderURL)
                refreshStatus()
                return
            }

            guard targetExists else {
                lastActionText = "No proxy to remove."
                refreshStatus()
                return
            }

            guard let bundledProxyURL else {
                lastActionText = "Bundled dinput8.dll not found. Remove skipped."
                refreshStatus()
                return
            }

            if filesHaveSameSHA256(targetProxyURL, bundledProxyURL) {
                try fileManager.removeItem(at: targetProxyURL)
                lastActionText = "Proxy removed."
                recordHistory(action: "Removed", folderURL: gameFolderURL)
            } else {
                lastActionText = "Different dinput8.dll found. Remove skipped for safety."
                recordHistory(action: "Remove skipped - different DLL", folderURL: gameFolderURL)
            }
        } catch {
            lastActionText = "Remove failed: \(error.localizedDescription)"
        }

        refreshStatus()
    }

    func clearHistory() {
        UserDefaults.standard.removeObject(forKey: DefaultsKey.installHistory)
        historyDisplayText = "No install history yet"
        lastActionText = "Install history cleared."
        refreshStatus()
    }

    private func updateProxyStatus(gameFolderURL: URL) {
        let fileManager = FileManager.default
        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        let targetExists = fileManager.fileExists(atPath: targetProxyURL.path)
        let backupExists = fileManager.fileExists(atPath: backupURL.path)

        guard let bundledProxyURL else {
            proxyStatusText = "Proxy source missing"
            proxyStatusIcon = "xmark.circle"
            proxyStatusColor = .red
            return
        }

        guard targetExists else {
            proxyStatusText = backupExists ? "Not installed, backup found" : "Not installed"
            proxyStatusIcon = backupExists ? "exclamationmark.triangle" : "minus.circle"
            proxyStatusColor = backupExists ? .orange : .secondary
            return
        }

        if filesHaveSameSHA256(targetProxyURL, bundledProxyURL) {
            proxyStatusText = "Installed"
            proxyStatusIcon = "checkmark.circle"
            proxyStatusColor = .green
        } else {
            proxyStatusText = "Different dinput8.dll found"
            proxyStatusIcon = "exclamationmark.triangle"
            proxyStatusColor = .orange
        }
    }

    private func loadHistory() {
        let entries = UserDefaults.standard.stringArray(forKey: DefaultsKey.installHistory) ?? []
        historyDisplayText = entries.isEmpty ? "No install history yet" : entries.joined(separator: "\n")
    }

    private func recordHistory(action: String, folderURL: URL) {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"

        let entry = "[\(formatter.string(from: Date()))] \(action): \(folderURL.path)"

        var entries = UserDefaults.standard.stringArray(forKey: DefaultsKey.installHistory) ?? []
        entries.append(entry)

        if entries.count > 30 {
            entries = Array(entries.suffix(30))
        }

        UserDefaults.standard.set(entries, forKey: DefaultsKey.installHistory)
        historyDisplayText = entries.joined(separator: "\n")
    }

    private func findExeFiles(in folderURL: URL) -> [URL] {
        guard let contents = try? FileManager.default.contentsOfDirectory(
            at: folderURL,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            return []
        }

        return contents
            .filter { $0.pathExtension.lowercased() == "exe" }
            .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending }
    }

    private func filesHaveSameSHA256(_ firstURL: URL, _ secondURL: URL) -> Bool {
        guard let firstHash = sha256(firstURL), let secondHash = sha256(secondURL) else {
            return false
        }

        return firstHash == secondHash
    }

    private func sha256(_ url: URL) -> String? {
        guard let data = try? Data(contentsOf: url) else {
            return nil
        }

        let digest = SHA256.hash(data: data)
        return digest.map { String(format: "%02x", $0) }.joined()
    }
}
