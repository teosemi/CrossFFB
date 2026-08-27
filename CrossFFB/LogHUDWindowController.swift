//
//  LogHUDWindowController.swift
//  CrossFFB
//

import SwiftUI
import AppKit

/// The log lives in a translucent panel pinned to the top left of the screen,
/// rather than stretching the menu bar panel from 310 to 470 points as it used
/// to. It floats over a game running windowed or borderless; nothing can float
/// over an exclusive-fullscreen game.
@MainActor
final class LogHUDWindowController {
    static let shared = LogHUDWindowController()

    private var panel: NSPanel?

    private init() {}

    var isVisible: Bool {
        panel?.isVisible ?? false
    }

    func setVisible(_ visible: Bool, bridgeManager: BridgeManager) {
        if visible {
            show(bridgeManager: bridgeManager)
        } else {
            hide()
        }
    }

    func show(bridgeManager: BridgeManager) {
        let panel = panel ?? makePanel(bridgeManager: bridgeManager)
        self.panel = panel

        positionTopLeft(panel)
        panel.orderFrontRegardless()
    }

    func hide() {
        panel?.orderOut(nil)
    }

    func close() {
        panel?.orderOut(nil)
        panel = nil
    }

    private func makePanel(bridgeManager: BridgeManager) -> NSPanel {
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 306, height: 230),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        panel.isFloatingPanel = true
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        panel.backgroundColor = .clear
        panel.isOpaque = false
        panel.hasShadow = true
        panel.hidesOnDeactivate = false
        panel.isMovableByWindowBackground = true
        panel.isReleasedWhenClosed = false

        let hosting = NSHostingView(
            rootView: LogHUDView(bridgeManager: bridgeManager) { [weak self] in
                self?.hide()
                bridgeManager.setLogVisible(false)
            }
        )

        panel.contentView = hosting
        return panel
    }

    private func positionTopLeft(_ panel: NSPanel) {
        guard let screen = NSScreen.main else {
            return
        }

        let margin: CGFloat = 16
        let frame = screen.visibleFrame
        let size = panel.frame.size

        panel.setFrameOrigin(
            NSPoint(
                x: frame.minX + margin,
                y: frame.maxY - size.height - margin
            )
        )
    }
}
