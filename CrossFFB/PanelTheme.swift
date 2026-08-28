//
//  PanelTheme.swift
//  CrossFFB
//

import SwiftUI

/// Colours for the menu bar panel. Black on a dark Mac, white on a light one,
/// blue on both, following the system appearance rather than imposing a look.
struct PanelTheme {
    let panel: Color
    let panelBorder: Color
    let surface: Color
    let surfaceFill: Color
    let accent: Color
    let accentText: Color
    let numeral: Color
    let label: Color
    let icon: Color
    let dim: Color
    let track: Color
    let rule: Color
    let lamp: Color
    let lampOff: Color
    let warning: Color
    let body: Color

    static func forScheme(_ scheme: ColorScheme) -> PanelTheme {
        scheme == .dark ? .dark : .light
    }

    // macOS uses a lighter blue on dark and a deeper one on light; matching that
    // keeps the accent from going murky on either background.
    static let dark = PanelTheme(
        panel: Color(red: 0.086, green: 0.090, blue: 0.102),
        panelBorder: Color.white.opacity(0.09),
        surface: Color.white.opacity(0.05),
        surfaceFill: Color(red: 0.039, green: 0.518, blue: 1.0).opacity(0.22),
        accent: Color(red: 0.039, green: 0.518, blue: 1.0),
        accentText: Color(red: 0.039, green: 0.518, blue: 1.0),
        numeral: .white,
        label: Color.white.opacity(0.5),
        icon: Color.white.opacity(0.75),
        dim: Color.white.opacity(0.35),
        track: Color.white.opacity(0.12),
        rule: Color.white.opacity(0.09),
        lamp: Color(red: 0.239, green: 0.863, blue: 0.420),
        lampOff: Color.white.opacity(0.18),
        warning: Color(red: 1.0, green: 0.690, blue: 0.125),
        body: Color.white.opacity(0.72)
    )

    static let light = PanelTheme(
        panel: Color(red: 0.968, green: 0.964, blue: 0.956),
        panelBorder: Color.black.opacity(0.12),
        surface: .white,
        surfaceFill: Color(red: 0.0, green: 0.478, blue: 1.0).opacity(0.16),
        accent: Color(red: 0.0, green: 0.478, blue: 1.0),
        accentText: Color(red: 0.0, green: 0.376, blue: 0.874),
        numeral: Color(red: 0.102, green: 0.102, blue: 0.110),
        label: Color.black.opacity(0.5),
        icon: Color.black.opacity(0.7),
        dim: Color.black.opacity(0.35),
        track: Color.black.opacity(0.10),
        rule: Color.black.opacity(0.09),
        lamp: Color(red: 0.133, green: 0.698, blue: 0.298),
        lampOff: Color.black.opacity(0.18),
        // A pale amber vanishes on white, so the light theme takes a darker one.
        warning: Color(red: 0.722, green: 0.455, blue: 0.0),
        body: Color.black.opacity(0.72)
    )
}

extension Font {
    /// The mockups used Barlow Condensed. The system font's condensed width gets
    /// the same instrument feel without bundling a face and its licence.
    static func condensed(_ size: CGFloat, weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight).width(.condensed)
    }
}
