//
//  WindowsExecutable.swift
//  CrossFFB
//

import Foundation

/// Architecture of a Windows executable, as declared by its PE header.
enum WindowsExecutableArchitecture: Equatable {
    case x64
    case x86
    case arm64
    case unrecognized

    var displayName: String {
        switch self {
        case .x64:
            return "64-bit"
        case .x86:
            return "32-bit"
        case .arm64:
            return "ARM64"
        case .unrecognized:
            return "unknown"
        }
    }

    /// The bundled dinput8.dll proxy is 64-bit, so only x64 games can load it.
    var canLoadProxy: Bool {
        self == .x64
    }
}

enum WindowsExecutable {
    private enum MachineType {
        static let i386: UInt16 = 0x014C
        static let amd64: UInt16 = 0x8664
        static let arm64: UInt16 = 0xAA64
    }

    /// Reads the COFF machine type of a PE file, touching only the few header
    /// bytes it needs instead of loading the whole executable.
    static func architecture(of url: URL) -> WindowsExecutableArchitecture {
        guard let handle = try? FileHandle(forReadingFrom: url) else {
            return .unrecognized
        }

        defer {
            try? handle.close()
        }

        guard let dosHeader = try? handle.read(upToCount: 64), dosHeader.count == 64 else {
            return .unrecognized
        }

        // "MZ"
        guard dosHeader[0] == 0x4D, dosHeader[1] == 0x5A else {
            return .unrecognized
        }

        // e_lfanew: offset of the PE header, little endian at 0x3C.
        let peOffset = UInt32(dosHeader[0x3C])
            | UInt32(dosHeader[0x3D]) << 8
            | UInt32(dosHeader[0x3E]) << 16
            | UInt32(dosHeader[0x3F]) << 24

        do {
            try handle.seek(toOffset: UInt64(peOffset))
        } catch {
            return .unrecognized
        }

        guard let coffHeader = try? handle.read(upToCount: 6), coffHeader.count == 6 else {
            return .unrecognized
        }

        // "PE\0\0"
        guard coffHeader[0] == 0x50, coffHeader[1] == 0x45,
              coffHeader[2] == 0x00, coffHeader[3] == 0x00 else {
            return .unrecognized
        }

        let machine = UInt16(coffHeader[4]) | UInt16(coffHeader[5]) << 8

        switch machine {
        case MachineType.amd64:
            return .x64
        case MachineType.i386:
            return .x86
        case MachineType.arm64:
            return .arm64
        default:
            return .unrecognized
        }
    }
}
