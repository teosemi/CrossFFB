//
//  CrossFFBApp.swift
//  CrossFFB
//
//  Created by teo on 13/05/2026.
//

import SwiftUI

@main
struct CrossFFBApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var bridgeManager = BridgeManager.shared

    var body: some Scene {
        MenuBarExtra {
            MenuBarView(bridgeManager: bridgeManager)
        } label: {
            Image(systemName: bridgeManager.isRunning ? "steeringwheel.circle.fill" : "steeringwheel.circle")
        }
        .menuBarExtraStyle(.window)
    }
}
