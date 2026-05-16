//
//  BridgeManager.swift
//  CrossFFB
//
//  Created by teo on 16/05/2026.
//

import Foundation
import AppKit
import Combine
import Network

@MainActor
final class BridgeManager: ObservableObject {
    static let shared = BridgeManager()

    private enum DefaultsKey {
        static let gain = "crossffb.gain"
        static let rangeDegrees = "crossffb.rangeDegrees"
        static let isLogVisible = "crossffb.isLogVisible"
    }

    @Published var isRunning: Bool = false
    @Published var statusText: String = "Stopped"
    @Published var isLogVisible: Bool
    @Published var logText: String = ""

    @Published var gain: Double
    @Published var rangeDegrees: Double

    private var process: Process?
    private var outputPipe: Pipe?

    private var gainDebounceTask: Task<Void, Never>?
    private var rangeDebounceTask: Task<Void, Never>?

    private var lastSentGain: Double?
    private var lastSentRangeDegrees: Double?

    private let gamePort: Int = 54321
    private let controlPort: UInt16 = 54322
    private let maxLogLines: Int = 300

    private var bridgeURL: URL? {
        if let bundledBridgeURL = Bundle.main.url(forResource: "g29_ffb_bridge", withExtension: nil) {
            return bundledBridgeURL
        }

        let developmentBridgeURL = URL(fileURLWithPath: "/Users/teo/CrossWheelTest/mac_bridge/g29_ffb_bridge")
        if FileManager.default.fileExists(atPath: developmentBridgeURL.path) {
            return developmentBridgeURL
        }

        return nil
    }

    private init() {
        let defaults = UserDefaults.standard

        if defaults.object(forKey: DefaultsKey.gain) == nil {
            gain = 1.00
        } else {
            gain = defaults.double(forKey: DefaultsKey.gain)
        }

        if defaults.object(forKey: DefaultsKey.rangeDegrees) == nil {
            rangeDegrees = 900
        } else {
            rangeDegrees = defaults.double(forKey: DefaultsKey.rangeDegrees)
        }

        if defaults.object(forKey: DefaultsKey.isLogVisible) == nil {
            isLogVisible = false
        } else {
            isLogVisible = defaults.bool(forKey: DefaultsKey.isLogVisible)
        }

        gain = Self.clamp(gain, min: 0.0, max: 1.5)
        rangeDegrees = Self.clamp(rangeDegrees, min: 40, max: 900)
    }

    func startIfNeeded() {
        guard !isRunning else {
            return
        }

        start()
    }

    func start() {
        guard !isRunning else {
            return
        }

        guard let bridgeURL else {
            statusText = "Bridge file missing"
            appendLog("CrossFFB: bridge file missing. Add g29_ffb_bridge to the app bundle or keep it at /Users/teo/CrossWheelTest/mac_bridge/g29_ffb_bridge")
            return
        }

        guard FileManager.default.fileExists(atPath: bridgeURL.path) else {
            statusText = "Bridge file missing"
            appendLog("CrossFFB: bridge file missing at \(bridgeURL.path)")
            return
        }

        guard FileManager.default.isExecutableFile(atPath: bridgeURL.path) else {
            statusText = "Bridge not executable"
            appendLog("CrossFFB: bridge is not executable at \(bridgeURL.path)")
            return
        }

        lastSentGain = nil
        lastSentRangeDegrees = nil

        let process = Process()
        let outputPipe = Pipe()

        process.executableURL = bridgeURL
        process.currentDirectoryURL = bridgeURL.deletingLastPathComponent()
        process.arguments = [
            "--gain", String(format: "%.2f", gain),
            "--range", String(Int(rangeDegrees.rounded())),
            "--invert", "0",
            "--port", "\(gamePort)",
            "--control-port", "\(controlPort)"
        ]

        process.standardOutput = outputPipe
        process.standardError = outputPipe

        appendLog("CrossFFB: starting bridge")
        appendLog("CrossFFB: \(bridgeURL.path) \(process.arguments?.joined(separator: " ") ?? "")")

        outputPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData

            guard !data.isEmpty else {
                return
            }

            guard let text = String(data: data, encoding: .utf8) else {
                return
            }

            guard let manager = self else {
                return
            }

            Task { @MainActor in
                manager.appendLog(text)
                manager.handleBridgeOutput(text)
            }
        }

        process.terminationHandler = { [weak self] process in
            let terminationStatus = process.terminationStatus

            guard let manager = self else {
                return
            }

            Task { @MainActor in
                manager.outputPipe?.fileHandleForReading.readabilityHandler = nil
                manager.process = nil
                manager.outputPipe = nil
                manager.isRunning = false
                manager.lastSentGain = nil
                manager.lastSentRangeDegrees = nil

                if manager.statusText == "Wheel not found" {
                    manager.appendLog("CrossFFB: bridge stopped because the wheel was not found")
                    return
                }

                if terminationStatus == 0 || terminationStatus == 2 || terminationStatus == 15 {
                    manager.statusText = "Stopped"
                    manager.appendLog("CrossFFB: bridge stopped")
                } else {
                    manager.statusText = "Bridge error code \(terminationStatus)"
                    manager.appendLog("CrossFFB: bridge stopped with code \(terminationStatus)")
                }
            }
        }

        do {
            try process.run()
            self.process = process
            self.outputPipe = outputPipe
            isRunning = true
            statusText = "Starting..."
        } catch {
            outputPipe.fileHandleForReading.readabilityHandler = nil
            self.process = nil
            self.outputPipe = nil
            isRunning = false
            lastSentGain = nil
            lastSentRangeDegrees = nil
            statusText = "Start failed: \(error.localizedDescription)"
            appendLog("CrossFFB: start failed: \(error.localizedDescription)")
        }
    }

    func stop() {
        gainDebounceTask?.cancel()
        rangeDebounceTask?.cancel()

        guard let process else {
            isRunning = false
            statusText = "Stopped"
            appendLog("CrossFFB: stop requested but bridge is not running")
            return
        }

        statusText = "Stopping..."
        appendLog("CrossFFB: stopping bridge")
        process.interrupt()
    }

    func stopForAppTermination() {
        gainDebounceTask?.cancel()
        rangeDebounceTask?.cancel()

        guard let process else {
            isRunning = false
            statusText = "Stopped"
            return
        }

        outputPipe?.fileHandleForReading.readabilityHandler = nil

        if process.isRunning {
            process.interrupt()

            let deadline = Date().addingTimeInterval(2.0)
            while process.isRunning && Date() < deadline {
                RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
            }
        }

        if process.isRunning {
            process.terminate()

            let deadline = Date().addingTimeInterval(1.0)
            while process.isRunning && Date() < deadline {
                RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
            }
        }

        self.process = nil
        self.outputPipe = nil
        isRunning = false
        statusText = "Stopped"
        lastSentGain = nil
        lastSentRangeDegrees = nil
    }

    func setGain(_ newValue: Double) {
        let clampedValue = Self.clamp(newValue, min: 0.0, max: 1.5)
        gain = clampedValue
        UserDefaults.standard.set(clampedValue, forKey: DefaultsKey.gain)

        guard isRunning else {
            return
        }

        gainDebounceTask?.cancel()

        gainDebounceTask = Task { [weak self] in
            do {
                try await Task.sleep(nanoseconds: 180_000_000)
            } catch {
                return
            }

            guard !Task.isCancelled else {
                return
            }

            guard let self else {
                return
            }

            await self.sendGainIfNeeded()
        }
    }

    func setRange(_ newValue: Double) {
        let roundedValue = (newValue / 10.0).rounded() * 10.0
        let clampedValue = Self.clamp(roundedValue, min: 40, max: 900)
        rangeDegrees = clampedValue
        UserDefaults.standard.set(clampedValue, forKey: DefaultsKey.rangeDegrees)

        guard isRunning else {
            return
        }

        rangeDebounceTask?.cancel()

        rangeDebounceTask = Task { [weak self] in
            do {
                try await Task.sleep(nanoseconds: 250_000_000)
            } catch {
                return
            }

            guard !Task.isCancelled else {
                return
            }

            guard let self else {
                return
            }

            await self.sendRangeIfNeeded()
        }
    }

    func applyRangePreset(_ degrees: Double) {
        rangeDebounceTask?.cancel()

        let clampedValue = Self.clamp(degrees, min: 40, max: 900)
        rangeDegrees = clampedValue
        UserDefaults.standard.set(clampedValue, forKey: DefaultsKey.rangeDegrees)

        guard isRunning else {
            return
        }

        Task {
            await sendRangeIfNeeded(force: true)
        }
    }

    func resetWheel() {
        guard isRunning else {
            return
        }

        Task {
            await sendControlCommand("RESET_WHEEL")
        }
    }

    func toggleLogVisibility() {
        isLogVisible.toggle()
        UserDefaults.standard.set(isLogVisible, forKey: DefaultsKey.isLogVisible)
    }

    func clearLog() {
        logText = ""
    }

    private func sendGainIfNeeded(force: Bool = false) async {
        let value = Self.clamp(gain, min: 0.0, max: 1.5)
        let roundedValue = (value * 100.0).rounded() / 100.0

        if !force, let lastSentGain, abs(lastSentGain - roundedValue) < 0.001 {
            return
        }

        lastSentGain = roundedValue
        await sendControlCommand("SET_GAIN \(String(format: "%.2f", roundedValue))")
    }

    private func sendRangeIfNeeded(force: Bool = false) async {
        let roundedValue = (rangeDegrees / 10.0).rounded() * 10.0
        let clampedValue = Self.clamp(roundedValue, min: 40, max: 900)

        if !force, let lastSentRangeDegrees, abs(lastSentRangeDegrees - clampedValue) < 0.001 {
            return
        }

        lastSentRangeDegrees = clampedValue
        await sendControlCommand("SET_RANGE \(Int(clampedValue.rounded()))")
    }

    private func sendControlCommand(_ command: String) async {
        let host = NWEndpoint.Host("127.0.0.1")
        guard let port = NWEndpoint.Port(rawValue: controlPort) else {
            statusText = "Invalid control port"
            appendLog("CrossFFB: invalid control port")
            return
        }

        appendLog("CrossFFB -> \(command)")

        let connection = NWConnection(host: host, port: port, using: .tcp)

        connection.stateUpdateHandler = { [weak self] state in
            switch state {
            case .failed:
                if let manager = self {
                    Task { @MainActor in
                        manager.statusText = "Control failed"
                        manager.appendLog("CrossFFB: control connection failed")
                    }
                }
                connection.cancel()

            default:
                break
            }
        }

        connection.start(queue: .global(qos: .userInitiated))

        let payload = Data((command + "\n").utf8)

        connection.send(content: payload, completion: .contentProcessed { error in
            if error != nil {
                Task { @MainActor in
                    self.statusText = "Control send failed"
                    self.appendLog("CrossFFB: control send failed")
                }

                connection.cancel()
                return
            }

            connection.receive(minimumIncompleteLength: 1, maximumLength: 1024) { [weak self] data, _, _, _ in
                if let data, let reply = String(data: data, encoding: .utf8), let manager = self {
                    Task { @MainActor in
                        manager.appendLog("Bridge -> \(reply.trimmingCharacters(in: .whitespacesAndNewlines))")
                        manager.handleControlReply(reply)
                    }
                }

                connection.cancel()
            }
        })
    }

    private func handleControlReply(_ reply: String) {
        let trimmed = reply.trimmingCharacters(in: .whitespacesAndNewlines)

        guard !trimmed.isEmpty else {
            return
        }

        if trimmed.hasPrefix("OK SET_GAIN") {
            statusText = "Gain updated"
            return
        }

        if trimmed.hasPrefix("OK SET_RANGE") {
            statusText = "Range updated"
            return
        }

        if trimmed.hasPrefix("OK RESET_WHEEL") {
            statusText = "Wheel reset"
            return
        }

        if trimmed.hasPrefix("OK STATUS") {
            statusText = "Running"
            return
        }

        if trimmed.hasPrefix("ERR") {
            statusText = trimmed
            return
        }
    }

    private func handleBridgeOutput(_ text: String) {
        if text.contains("CONTROL TCP listening") {
            statusText = "Running"
            return
        }

        if text.contains("TCP client connected") {
            statusText = "Game connected"
            return
        }

        if text.contains("TCP client disconnected") {
            statusText = "Running"
            return
        }

        if text.contains("Failed to open wheel") {
            statusText = "Wheel not found"
            return
        }

        if text.contains("IOHIDDeviceOpen main joystick rc=0x00000000") {
            statusText = "Wheel connected"
            return
        }
    }

    private func appendLog(_ text: String) {
        let normalized = text.replacingOccurrences(of: "\r", with: "")
        var lines = logText.components(separatedBy: "\n")

        if lines.count == 1 && lines[0].isEmpty {
            lines = []
        }

        let newLines = normalized
            .components(separatedBy: "\n")
            .filter { !$0.isEmpty }

        lines.append(contentsOf: newLines)

        if lines.count > maxLogLines {
            lines = Array(lines.suffix(maxLogLines))
        }

        logText = lines.joined(separator: "\n")
    }

    private static func clamp(_ value: Double, min minValue: Double, max maxValue: Double) -> Double {
        Swift.max(minValue, Swift.min(maxValue, value))
    }
}
