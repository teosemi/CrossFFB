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
    @Published var exeStatusText: String = "No game folder selected"
    @Published var exeStatusIcon: String = "questionmark.circle"
    @Published var exeStatusColor: Color = .secondary

    @Published var proxyStatusText: String = "Proxy status unknown"
    @Published var proxyStatusIcon: String = "questionmark.circle"
    @Published var proxyStatusColor: Color = .secondary

    @Published var detailText: String = ""
    @Published var lastActionText: String = ""
    @Published var historyDisplayText: String = "No install history yet"

    private let proxyFileName = "dinput8.dll"
    private let backupFileName = "dinput8.dll.crossffb_backup"

    private var bundledProxyURL: URL? {
        Bundle.main.url(forResource: "dinput8", withExtension: "dll")
    }

    var gameFolderDisplayText: String {
        guard let gameFolderURL else {
            return "No folder selected"
        }

        return gameFolderURL.path
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
        return FileManager.default.fileExists(atPath: targetProxyURL.path) || FileManager.default.fileExists(atPath: backupURL.path)
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
            exeStatusText = "No game folder selected"
            exeStatusIcon = "questionmark.circle"
            exeStatusColor = .secondary

            proxyStatusText = "Proxy status unknown"
            proxyStatusIcon = "questionmark.circle"
            proxyStatusColor = .secondary

            detailText = ""
            return
        }

        let fileManager = FileManager.default
        var isDirectory: ObjCBool = false

        guard fileManager.fileExists(atPath: gameFolderURL.path, isDirectory: &isDirectory), isDirectory.boolValue else {
            exeStatusText = "Selected folder does not exist"
            exeStatusIcon = "xmark.circle"
            exeStatusColor = .red

            proxyStatusText = "Proxy status unknown"
            proxyStatusIcon = "questionmark.circle"
            proxyStatusColor = .secondary

            detailText = gameFolderURL.path
            return
        }

        let exeFiles = findExeFiles(in: gameFolderURL)

        if exeFiles.isEmpty {
            exeStatusText = "No .exe found in selected folder"
            exeStatusIcon = "exclamationmark.triangle"
            exeStatusColor = .orange
        } else if exeFiles.count == 1 {
            exeStatusText = "Found executable: \(exeFiles[0].lastPathComponent)"
            exeStatusIcon = "checkmark.circle"
            exeStatusColor = .green
        } else {
            exeStatusText = "Found \(exeFiles.count) executables"
            exeStatusIcon = "checkmark.circle"
            exeStatusColor = .green
        }

        updateProxyStatus(gameFolderURL: gameFolderURL, exeFiles: exeFiles)
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
            lastActionText = "Bundled dinput8.dll not found. Add dinput8.dll to Copy Bundle Resources."
            refreshStatus()
            return
        }

        let fileManager = FileManager.default
        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        do {
            if fileManager.fileExists(atPath: targetProxyURL.path) {
                if filesHaveSameSHA256(targetProxyURL, bundledProxyURL) {
                    lastActionText = "CrossFFB proxy is already installed."
                    recordHistory(action: "Already installed", folderURL: gameFolderURL)
                    refreshStatus()
                    return
                }

                if !fileManager.fileExists(atPath: backupURL.path) {
                    try fileManager.copyItem(at: targetProxyURL, to: backupURL)
                    lastActionText = "Existing dinput8.dll backed up and CrossFFB proxy installed."
                } else {
                    lastActionText = "Existing backup kept and CrossFFB proxy installed."
                }

                try fileManager.removeItem(at: targetProxyURL)
            } else {
                lastActionText = "CrossFFB proxy installed."
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
                lastActionText = "Proxy removed and previous dinput8.dll restored."
                recordHistory(action: "Removed and restored backup", folderURL: gameFolderURL)
                refreshStatus()
                return
            }

            guard targetExists else {
                lastActionText = "No dinput8.dll to remove."
                refreshStatus()
                return
            }

            guard let bundledProxyURL else {
                lastActionText = "Bundled dinput8.dll not found. Cannot safely remove target."
                refreshStatus()
                return
            }

            if filesHaveSameSHA256(targetProxyURL, bundledProxyURL) {
                try fileManager.removeItem(at: targetProxyURL)
                lastActionText = "CrossFFB proxy removed."
                recordHistory(action: "Removed", folderURL: gameFolderURL)
            } else {
                lastActionText = "Different dinput8.dll found. Not removed for safety."
                recordHistory(action: "Remove skipped - different DLL", folderURL: gameFolderURL)
            }
        } catch {
            lastActionText = "Remove failed: \(error.localizedDescription)"
        }

        refreshStatus()
    }

    private func updateProxyStatus(gameFolderURL: URL, exeFiles: [URL]) {
        let targetProxyURL = gameFolderURL.appendingPathComponent(proxyFileName)
        let backupURL = gameFolderURL.appendingPathComponent(backupFileName)

        let fileManager = FileManager.default
        let targetExists = fileManager.fileExists(atPath: targetProxyURL.path)
        let backupExists = fileManager.fileExists(atPath: backupURL.path)

        var detailLines: [String] = []
        detailLines.append("Folder: \(gameFolderURL.path)")

        if exeFiles.isEmpty {
            detailLines.append("EXE: none")
        } else {
            detailLines.append("EXE: \(exeFiles.map { $0.lastPathComponent }.joined(separator: ", "))")
        }

        guard let bundledProxyURL else {
            proxyStatusText = "Bundled dinput8.dll not found"
            proxyStatusIcon = "xmark.circle"
            proxyStatusColor = .red

            detailLines.append("Bundled proxy: missing")
            appendLastAction(to: &detailLines)
            detailText = detailLines.joined(separator: "\n")
            return
        }

        detailLines.append("Bundled proxy: \(bundledProxyURL.path)")

        guard targetExists else {
            proxyStatusText = backupExists ? "Proxy missing, backup found" : "Proxy not installed"
            proxyStatusIcon = backupExists ? "exclamationmark.triangle" : "minus.circle"
            proxyStatusColor = backupExists ? .orange : .secondary

            detailLines.append("Target dinput8.dll: missing")
            detailLines.append("Backup: \(backupExists ? "present" : "missing")")
            appendLastAction(to: &detailLines)
            detailText = detailLines.joined(separator: "\n")
            return
        }

        let sameAsBundled = filesHaveSameSHA256(targetProxyURL, bundledProxyURL)

        if sameAsBundled {
            proxyStatusText = "CrossFFB proxy installed"
            proxyStatusIcon = "checkmark.circle"
            proxyStatusColor = .green
        } else {
            proxyStatusText = backupExists ? "Different dinput8.dll installed, backup present" : "Different dinput8.dll installed"
            proxyStatusIcon = "exclamationmark.triangle"
            proxyStatusColor = .orange
        }

        detailLines.append("Target dinput8.dll: \(targetProxyURL.path)")
        detailLines.append("Target matches bundled proxy: \(sameAsBundled ? "yes" : "no")")
        detailLines.append("Backup: \(backupExists ? "present" : "missing")")
        appendLastAction(to: &detailLines)
        detailText = detailLines.joined(separator: "\n")
    }

    private func appendLastAction(to lines: inout [String]) {
        if !lastActionText.isEmpty {
            lines.append("Last action: \(lastActionText)")
        }
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

    func clearHistory() {
        UserDefaults.standard.removeObject(forKey: DefaultsKey.installHistory)
        historyDisplayText = "No install history yet"
        lastActionText = "Install history cleared."
        refreshStatus()
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
}
