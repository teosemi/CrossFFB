//
//  InstrumentControls.swift
//  CrossFFB
//

import SwiftUI

/// The half circle the steering range is drawn on.
private struct ArcShape: Shape {
    func path(in rect: CGRect) -> Path {
        let radius = min(rect.width / 2, rect.height) - 6
        let centre = CGPoint(x: rect.midX, y: rect.maxY - 6)

        var path = Path()
        path.addArc(
            center: centre,
            radius: max(radius, 1),
            startAngle: .degrees(180),
            endAngle: .degrees(360),
            clockwise: false
        )
        return path
    }
}

/// Steering range as an arc with the value in its mouth.
struct ArcGauge: View {
    let value: Double
    let bounds: ClosedRange<Double>
    let theme: PanelTheme

    private var fraction: Double {
        let span = bounds.upperBound - bounds.lowerBound
        guard span > 0 else { return 0 }
        return min(max((value - bounds.lowerBound) / span, 0), 1)
    }

    var body: some View {
        ZStack {
            ArcShape()
                .stroke(theme.track, style: StrokeStyle(lineWidth: 9, lineCap: .round))

            ArcShape()
                .trim(from: 0, to: fraction)
                .stroke(theme.accent, style: StrokeStyle(lineWidth: 9, lineCap: .round))

            VStack(spacing: 0) {
                Text("\(Int(value.rounded()))")
                    .font(.condensed(46))
                    .foregroundStyle(theme.numeral)

                Text("DEGREES")
                    .font(.condensed(11, weight: .medium))
                    .tracking(3)
                    .foregroundStyle(theme.dim)
            }
            .offset(y: 18)
        }
        .frame(height: 96)
        .accessibilityElement()
        .accessibilityLabel("Steering range")
        .accessibilityValue("\(Int(value.rounded())) degrees")
    }
}

/// A row of bars whose heights ramp left to right. Used for the force, which is
/// continuous, and scrubbable anywhere along its width.
struct StepScale: View {
    let fraction: Double
    let steps: Int
    let theme: PanelTheme
    var onScrub: ((Double) -> Void)?

    private func isLit(_ index: Int) -> Bool {
        Double(index) < (fraction * Double(steps)).rounded()
    }

    var body: some View {
        GeometryReader { geometry in
            HStack(alignment: .bottom, spacing: 3) {
                ForEach(0..<steps, id: \.self) { index in
                    RoundedRectangle(cornerRadius: 1)
                        .fill(isLit(index) ? theme.accent : theme.track)
                        .frame(height: barHeight(at: index))
                }
            }
            .frame(maxHeight: .infinity, alignment: .bottom)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        guard geometry.size.width > 0 else { return }
                        let ratio = drag.location.x / geometry.size.width
                        onScrub?(min(max(ratio, 0), 1))
                    }
            )
        }
        .frame(height: 22)
    }

    private func barHeight(at index: Int) -> CGFloat {
        guard steps > 1 else { return 22 }
        return 8 + (22 - 8) * CGFloat(index) / CGFloat(steps - 1)
    }
}

/// A tile you can drag anywhere on, filled to its value. Wide enough to grab
/// without aiming, which is the point of it.
struct ThickSlider: View {
    let title: String
    let value: String
    let fraction: Double
    let systemImage: String
    let theme: PanelTheme
    var onScrub: ((Double) -> Void)?

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 10)
                    .fill(theme.surface)

                theme.surfaceFill
                    .frame(width: geometry.size.width * CGFloat(min(max(fraction, 0), 1)))

                theme.accent
                    .frame(width: 2)
                    .offset(x: geometry.size.width * CGFloat(min(max(fraction, 0), 1)) - 2)
                    .opacity(fraction > 0.01 ? 1 : 0)

                HStack(spacing: 9) {
                    Image(systemName: systemImage)
                        .font(.system(size: 13))
                        .foregroundStyle(theme.icon)

                    Text(title)
                        .font(.condensed(14, weight: .medium))
                        .tracking(1.4)
                        .foregroundStyle(theme.label)

                    Spacer()

                    Text(value)
                        .font(.condensed(24))
                        .foregroundStyle(theme.numeral)
                }
                .padding(.horizontal, 14)
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        guard geometry.size.width > 0 else { return }
                        let ratio = drag.location.x / geometry.size.width
                        onScrub?(min(max(ratio, 0), 1))
                    }
            )
        }
        .frame(height: 52)
        .accessibilityElement()
        .accessibilityLabel(title)
        .accessibilityValue(value)
    }
}

/// The 540 / 720 / 900 / SET row under the arc.
struct PresetChips: View {
    let presets: [Int]
    let selected: Int?
    let theme: PanelTheme
    var onSelect: (Int) -> Void
    var onCustom: () -> Void

    var body: some View {
        HStack(spacing: 6) {
            ForEach(presets, id: \.self) { preset in
                chip(String(preset), isOn: preset == selected) {
                    onSelect(preset)
                }
            }

            chip("SET", isOn: selected == nil, action: onCustom)
        }
    }

    private func chip(_ text: String, isOn: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.condensed(13, weight: isOn ? .semibold : .medium))
                .tracking(0.8)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .foregroundStyle(isOn ? Color.white : theme.label)
                .background(
                    RoundedRectangle(cornerRadius: 6)
                        .fill(isOn ? theme.accent : Color.clear)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(isOn ? theme.accent : theme.track, lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
    }
}

/// Wheel and game lamps.
struct StatusLamp: View {
    let title: String
    let isOn: Bool
    let theme: PanelTheme

    var body: some View {
        HStack(spacing: 5) {
            Circle()
                .fill(isOn ? theme.lamp : theme.lampOff)
                .frame(width: 6, height: 6)
                .shadow(color: isOn ? theme.lamp.opacity(0.7) : .clear, radius: 3)

            Text(title)
                .font(.condensed(12, weight: .semibold))
                .tracking(1.2)
                .foregroundStyle(theme.label)
        }
    }
}
