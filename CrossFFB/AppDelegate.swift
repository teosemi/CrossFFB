//
//  AppDelegate.swift
//  CrossFFB
//
//  Created by teo on 16/05/2026.
//

import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationWillTerminate(_ notification: Notification) {
        BridgeManager.shared.stopForAppTermination()
    }
}
