//
//  AppDelegate.swift
//  CrossFFB
//
//  Created by teo on 16/05/2026.
//

import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // The menu bar view only appears when the user opens the popover, so
        // starting from there meant the bridge stayed down until the first
        // click, even though onboarding tells people to just launch the app.
        BridgeManager.shared.startIfNeeded()
    }

    func applicationWillTerminate(_ notification: Notification) {
        BridgeManager.shared.stopForAppTermination()
    }
}
