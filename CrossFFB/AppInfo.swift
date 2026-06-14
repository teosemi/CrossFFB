//
//  AppInfo.swift
//  CrossFFB
//
//  Created by teo on 17/05/2026.
//

import AppKit

enum AppInfo {
    static func showAboutPanel() {
        let appName = Bundle.main.object(forInfoDictionaryKey: "CFBundleName") as? String ?? "CrossFFB"
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0"
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"

        let creditsText = """
        Force Feedback bridge for Logitech G29 on macOS.

        Made by Matteo Seminara & Maurizio Seminara
        """

        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .center

        let credits = NSAttributedString(
            string: creditsText,
            attributes: [
                .font: NSFont.systemFont(ofSize: 12),
                .foregroundColor: NSColor.secondaryLabelColor,
                .paragraphStyle: paragraphStyle
            ]
        )

        NSApplication.shared.orderFrontStandardAboutPanel(
            options: [
                .applicationName: appName,
                .applicationVersion: version,
                .version: "Build \(build)",
                .credits: credits
            ]
        )
    }
}
