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
import IOKit

@MainActor
final class BridgeManager: ObservableObject {
    static let shared = BridgeManager()

    private enum DefaultsKey {
        static let gain = "crossffb.gain"
        static let rangeDegrees = "crossffb.rangeDegrees"
        static let isLogVisible = "crossffb.isLogVisible"
        static let damperGain = "crossffb.damperGain"
        static let hasSeenGameConnect = "crossffb.hasSeenGameConnect"
        static let isDamperEnabled = "crossffb.damperEnabled"
    }

    @Published var isRunning: Bool = false
    @Published var statusText: String = "Stopped"
    @Published var isLogVisible: Bool
    @Published var logText: String = ""

    @Published var gain: Double
    @Published var rangeDegrees: Double

    /// The panel shows wheel and game as separate lamps, so a dead wheel cannot
    /// hide behind a connected game.
    @Published var isWheelConnected: Bool = false
    @Published var isGameConnected: Bool = false

    /// A game that reaches the bridge proves the bottle override took, which is
    /// the one step CrossFFB cannot perform or inspect itself.
    @Published var hasSeenGameConnect: Bool = UserDefaults.standard.bool(forKey: DefaultsKey.hasSeenGameConnect)

    /// Condition damper, used by ACC and ignored by ETS2.
    @Published var isDamperEnabled: Bool
    @Published var damperGain: Double

    private var process: Process?
    private var outputPipe: Pipe?

    private var gainDebounceTask: Task<Void, Never>?
    private var rangeDebounceTask: Task<Void, Never>?
    private var damperGainDebounceTask: Task<Void, Never>?

    private var lastSentGain: Double?
    private var lastSentRangeDegrees: Double?
    private var lastSentDamperGain: Double?
    private var lastSentDamperEnabled: Bool?

    /// Set when the user presses Stop, so reopening the menu does not undo it.
    private var userRequestedStop = false

    /// Polls for the wheel after it goes missing, so plugging it back in is enough.
    private var wheelWatchTask: Task<Void, Never>?

    private let gamePort: Int = 54321
    private let controlPort: UInt16 = 54322
    private let maxLogLines: Int = 300

    private var bridgeURL: URL? {
        if let bundledBridgeURL = Bundle.main.url(forResource: "g29_ffb_bridge", withExtension: nil) {
            return bundledBridgeURL
        }

        if let overridePath = ProcessInfo.processInfo.environment["CROSSFFB_BRIDGE_PATH"],
           !overridePath.isEmpty {
            let overrideURL = URL(fileURLWithPath: overridePath)
            if FileManager.default.fileExists(atPath: overrideURL.path) {
                return overrideURL
            }
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

        if defaults.object(forKey: DefaultsKey.damperGain) == nil {
            damperGain = 1.00
        } else {
            damperGain = defaults.double(forKey: DefaultsKey.damperGain)
        }

        if defaults.object(forKey: DefaultsKey.isDamperEnabled) == nil {
            isDamperEnabled = true
        } else {
            isDamperEnabled = defaults.bool(forKey: DefaultsKey.isDamperEnabled)
        }

        damperGain = Self.clamp(damperGain, min: 0.0, max: 1.0)
        gain = Self.clamp(gain, min: 0.0, max: 1.5)
        rangeDegrees = Self.clamp(rangeDegrees, min: 40, max: 900)
    }

    private static let logitechVendorID = 0x046D
    private static let g29ProductID = 0xC24F

    /// Asks the IOKit registry whether the wheel is attached right now.
    private static func isWheelAttached() -> Bool {
        guard let matching = IOServiceMatching("IOHIDDevice") as NSMutableDictionary? else {
            return false
        }

        matching["VendorID"] = NSNumber(value: logitechVendorID)
        matching["ProductID"] = NSNumber(value: g29ProductID)

        var iterator: io_iterator_t = 0

        guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else {
            return false
        }

        defer {
            IOObjectRelease(iterator)
        }

        let service = IOIteratorNext(iterator)

        guard service != 0 else {
            return false
        }

        IOObjectRelease(service)
        return true
    }

    /// The bridge exits when the wheel is missing, so instead of respawning it
    /// into the same failure the app waits for the device to come back.
    private func waitForWheelThenStart() {
        wheelWatchTask?.cancel()

        wheelWatchTask = Task { [weak self] in
            while !Task.isCancelled {
                do {
                    try await Task.sleep(nanoseconds: 2_000_000_000)
                } catch {
                    return
                }

                guard let self, !Task.isCancelled else {
                    return
                }

                guard !self.userRequestedStop, !self.isRunning else {
                    return
                }

                if Self.isWheelAttached() {
                    self.appendLog("CrossFFB: wheel detected, starting bridge")
                    self.start()
                    return
                }
            }
        }
    }

    func startIfNeeded() {
        guard !isRunning, !userRequestedStop else {
            return
        }

        start()
    }

    func start() {
        guard !isRunning else {
            return
        }

        userRequestedStop = false
        wheelWatchTask?.cancel()
        wheelWatchTask = nil

        guard let bridgeURL else {
            statusText = "Bridge file missing"
            appendLog("CrossFFB: bridge file missing. Build it with scripts/prepare_resources.sh so it is embedded in the app bundle, or set CROSSFFB_BRIDGE_PATH to an existing g29_ffb_bridge binary.")
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
        lastSentDamperGain = nil
        lastSentDamperEnabled = nil

        let process = Process()
        let outputPipe = Pipe()

        process.executableURL = bridgeURL
        process.currentDirectoryURL = bridgeURL.deletingLastPathComponent()
        process.arguments = [
            "--gain", String(format: "%.2f", gain),
            "--range", String(Int(rangeDegrees.rounded())),
            "--invert", "0",
            "--damper", isDamperEnabled ? "1" : "0",
            "--damper-gain", String(format: "%.2f", damperGain),
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
                manager.isWheelConnected = false
                manager.isGameConnected = false
                manager.lastSentGain = nil
                manager.lastSentDamperGain = nil
                manager.lastSentDamperEnabled = nil
                manager.lastSentRangeDegrees = nil

                if manager.statusText == "Wheel not found" || manager.statusText == "Wheel disconnected" {
                    manager.appendLog("CrossFFB: bridge stopped, wheel unavailable (\(manager.statusText))")
                    manager.waitForWheelThenStart()
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
            isWheelConnected = false
            isGameConnected = false
            lastSentGain = nil
            lastSentRangeDegrees = nil
            lastSentDamperGain = nil
            lastSentDamperEnabled = nil
            statusText = "Start failed: \(error.localizedDescription)"
            appendLog("CrossFFB: start failed: \(error.localizedDescription)")
        }
    }

    func stop() {
        userRequestedStop = true
        wheelWatchTask?.cancel()
        wheelWatchTask = nil
        gainDebounceTask?.cancel()
        rangeDebounceTask?.cancel()
        damperGainDebounceTask?.cancel()

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
        lastSentDamperGain = nil
        lastSentDamperEnabled = nil
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

    func setDamperEnabled(_ newValue: Bool) {
        isDamperEnabled = newValue
        UserDefaults.standard.set(newValue, forKey: DefaultsKey.isDamperEnabled)

        guard isRunning else {
            return
        }

        Task { [weak self] in
            await self?.sendDamperEnabledIfNeeded()
        }
    }

    func setDamperGain(_ newValue: Double) {
        let clampedValue = Self.clamp(newValue, min: 0.0, max: 1.0)
        damperGain = clampedValue
        UserDefaults.standard.set(clampedValue, forKey: DefaultsKey.damperGain)

        guard isRunning else {
            return
        }

        damperGainDebounceTask?.cancel()

        damperGainDebounceTask = Task { [weak self] in
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

            await self.sendDamperGainIfNeeded()
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
            await sendDamperEnabledIfNeeded(force: true)
            await sendDamperGainIfNeeded(force: true)
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
        setLogVisible(!isLogVisible)
    }

    func setLogVisible(_ visible: Bool) {
        guard isLogVisible != visible else {
            return
        }

        isLogVisible = visible
        UserDefaults.standard.set(visible, forKey: DefaultsKey.isLogVisible)
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

    private func sendDamperEnabledIfNeeded(force: Bool = false) async {
        if !force, let lastSentDamperEnabled, lastSentDamperEnabled == isDamperEnabled {
            return
        }

        lastSentDamperEnabled = isDamperEnabled
        await sendControlCommand("SET_DAMPER \(isDamperEnabled ? 1 : 0)")
    }

    private func sendDamperGainIfNeeded(force: Bool = false) async {
        let value = Self.clamp(damperGain, min: 0.0, max: 1.0)
        let roundedValue = (value * 100.0).rounded() / 100.0

        if !force, let lastSentDamperGain, abs(lastSentDamperGain - roundedValue) < 0.001 {
            return
        }

        lastSentDamperGain = roundedValue
        await sendControlCommand("SET_DAMPER_GAIN \(String(format: "%.2f", roundedValue))")
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
            isGameConnected = true

            if !hasSeenGameConnect {
                hasSeenGameConnect = true
                UserDefaults.standard.set(true, forKey: DefaultsKey.hasSeenGameConnect)
            }

            statusText = "Game connected"
            return
        }

        if text.contains("TCP client disconnected") {
            isGameConnected = false
            statusText = "Running"
            return
        }

        if text.contains("Failed to open wheel") {
            isWheelConnected = false
            statusText = "Wheel not found"
            return
        }

        if text.contains("Wheel disconnected") {
            isWheelConnected = false
            isGameConnected = false
            statusText = "Wheel disconnected"
            return
        }

        if text.contains("IOHIDDeviceOpen main joystick rc=0x00000000") {
            isWheelConnected = true
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
