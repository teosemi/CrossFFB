//
//  InstrumentControls.swift
//  CrossFFB
//

import SwiftUI

/// The half circle the steering range is drawn on.
private enum ArcGeometry {
    static func radius(in size: CGSize) -> CGFloat {
        max(min(size.width / 2, size.height) - 6, 1)
    }

    static func centre(in size: CGSize) -> CGPoint {
        CGPoint(x: size.width / 2, y: size.height - 6)
    }

    /// Where the knob sits for a given fraction of the arc.
    static func point(for fraction: Double, in size: CGSize) -> CGPoint {
        let angle = CGFloat.pi * (1 - min(max(fraction, 0), 1))
        let centre = centre(in: size)
        let radius = radius(in: size)

        return CGPoint(
            x: centre.x + radius * cos(angle),
            y: centre.y - radius * sin(angle)
        )
    }
}

private struct ArcShape: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.addArc(
            center: ArcGeometry.centre(in: rect.size),
            radius: ArcGeometry.radius(in: rect.size),
            startAngle: .degrees(180),
            endAngle: .degrees(360),
            clockwise: false
        )
        return path
    }
}


/// Which number is being typed, if any. Held by the panel so only one field is
/// ever open and a click anywhere else closes it.
enum PanelField: Hashable {
    case range
    case force
    case damper
}

/// Any value in the panel can be typed: double click the number and it becomes
/// a field. Enter commits; Escape, clicking elsewhere or losing focus cancels.
struct EditableValue: View {
    let field: PanelField
    @Binding var editingField: PanelField?

    let display: String
    let editSeed: String
    let font: Font
    let foreground: Color
    let accent: Color
    let fieldWidth: CGFloat
    let onCommit: (Double) -> Void

    @State private var draft = ""
    @FocusState private var isFocused: Bool

    private var isEditing: Bool {
        editingField == field
    }

    var body: some View {
        if isEditing {
            TextField("", text: $draft)
                .textFieldStyle(.plain)
                .font(font)
                .foregroundStyle(foreground)
                .multilineTextAlignment(.center)
                .focused($isFocused)
                .frame(width: fieldWidth)
                .padding(.horizontal, 6)
                .padding(.vertical, 1)
                .background(
                    RoundedRectangle(cornerRadius: 5)
                        .stroke(accent, lineWidth: 1)
                )
                .onSubmit(commit)
                .onExitCommand { editingField = nil }
                .onChange(of: isFocused) { _, focused in
                    if !focused {
                        editingField = nil
                    }
                }
                .onAppear {
                    isFocused = true
                }
        } else {
            Text(display)
                .font(font)
                .foregroundStyle(foreground)
                .contentShape(Rectangle())
                .onTapGesture(count: 2) {
                    draft = editSeed
                    editingField = field
                }
                .help("Double click to type a value")
        }
    }

    private func commit() {
        let cleaned = draft
            .replacingOccurrences(of: ",", with: ".")
            .trimmingCharacters(in: .whitespaces)

        if let value = Double(cleaned) {
            onCommit(value)
        }

        editingField = nil
    }
}

/// Steering range as an arc with the value in its mouth.
struct ArcGauge<Centre: View>: View {
    let value: Double
    let bounds: ClosedRange<Double>
    let theme: PanelTheme
    var onScrub: ((Double) -> Void)?
    @ViewBuilder var centre: () -> Centre

    private var fraction: Double {
        let span = bounds.upperBound - bounds.lowerBound
        guard span > 0 else { return 0 }
        return min(max((value - bounds.lowerBound) / span, 0), 1)
    }

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                ArcShape()
                    .stroke(theme.track, style: StrokeStyle(lineWidth: 9, lineCap: .round))

                ArcShape()
                    .trim(from: 0, to: fraction)
                    .stroke(theme.accent, style: StrokeStyle(lineWidth: 9, lineCap: .round))

                Circle()
                    .fill(Color.white)
                    .overlay(
                        Circle()
                            .stroke(theme.accent, lineWidth: 2)
                    )
                    .frame(width: 14, height: 14)
                    .shadow(color: .black.opacity(0.3), radius: 2, y: 1)
                    .position(ArcGeometry.point(for: fraction, in: geometry.size))

                centre()
                    .offset(y: 18)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        scrub(drag, in: geometry.size)
                    }
            )
        }
        .frame(height: 96)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Steering range")
        .accessibilityValue("\(Int(value.rounded())) degrees")
    }

    /// The arc is grabbable along its band only, so a double click on the number
    /// in the middle still reaches the number.
    private func scrub(_ drag: DragGesture.Value, in size: CGSize) {
        let radius = ArcGeometry.radius(in: size)
        let centrePoint = ArcGeometry.centre(in: size)

        let start = CGPoint(
            x: drag.startLocation.x - centrePoint.x,
            y: drag.startLocation.y - centrePoint.y
        )

        guard hypot(start.x, start.y) > radius * 0.55 else { return }

        let current = CGPoint(
            x: drag.location.x - centrePoint.x,
            y: drag.location.y - centrePoint.y
        )

        let ratio: Double

        if current.y > 0 {
            // Dragged below the ends of the arc. Which end it left through
            // decides where it stops: clamping the angle alone sent the left end
            // to the maximum, so the range snapped from 40 back to 900.
            ratio = current.x < 0 ? 0 : 1
        } else {
            // Angle with y pointing up: pi at the left end, 0 at the right.
            let angle = atan2(-current.y, current.x)
            ratio = 1 - min(max(angle, 0), .pi) / .pi
        }

        onScrub?(bounds.lowerBound + ratio * (bounds.upperBound - bounds.lowerBound))
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
struct ThickSlider<Value: View>: View {
    let title: String
    let fraction: Double
    let systemImage: String
    let theme: PanelTheme
    var onScrub: ((Double) -> Void)?
    @ViewBuilder var value: () -> Value

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

                    value()
                }
                .padding(.horizontal, 14)
            }
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .contentShape(Rectangle())
            .gesture(
                // A few points of travel before this counts as a drag, so the
                // whole tile stays grabbable - the value sits at the right hand
                // end, and a double click on it must not slam the tile to full
                // before opening the field.
                DragGesture(minimumDistance: 3)
                    .onChanged { drag in
                        guard geometry.size.width > 0 else { return }

                        let ratio = drag.location.x / geometry.size.width
                        onScrub?(min(max(ratio, 0), 1))
                    }
            )
        }
        .frame(height: 52)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(title)
    }
}

/// The 540 / 720 / 900 row under the arc. An exact angle is typed on the number
/// itself, so there is no SET chip.
struct PresetChips: View {
    let presets: [Int]
    let selected: Int?
    let theme: PanelTheme
    var onSelect: (Int) -> Void

    var body: some View {
        HStack(spacing: 6) {
            ForEach(presets, id: \.self) { preset in
                Button {
                    onSelect(preset)
                } label: {
                    Text(String(preset))
                        .font(.condensed(13, weight: preset == selected ? .semibold : .medium))
                        .tracking(0.8)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 5)
                        .foregroundStyle(preset == selected ? Color.white : theme.label)
                        .background(
                            RoundedRectangle(cornerRadius: 6)
                                .fill(preset == selected ? theme.accent : Color.clear)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 6)
                                .stroke(preset == selected ? theme.accent : theme.track, lineWidth: 1)
                        )
                }
                .buttonStyle(.plain)
            }
        }
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

/// A bordered button, or a filled one for the single action that matters on a
/// screen. Used by Setup and the onboarding checklist.
struct InstrumentButtonStyle: ButtonStyle {
    let theme: PanelTheme
    var isProminent: Bool = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12, weight: isProminent ? .semibold : .regular))
            .foregroundStyle(isProminent ? Color.white : theme.body)
            .padding(.horizontal, 14)
            .padding(.vertical, 6)
            .background(
                RoundedRectangle(cornerRadius: 7)
                    .fill(isProminent ? theme.accent : Color.clear)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 7)
                    .stroke(isProminent ? Color.clear : theme.track, lineWidth: 1)
            )
            .opacity(configuration.isPressed ? 0.7 : 1)
            .contentShape(Rectangle())
    }
}

/// One line of the onboarding checklist.
struct ChecklistRow: View {
    enum State {
        case done
        case todo
        /// Something CrossFFB cannot do for you.
        case attention
    }

    struct Action {
        let title: String
        var isProminent: Bool = false
        let perform: () -> Void
    }

    let title: String
    let detail: String
    let state: State
    let theme: PanelTheme
    let action: Action?
    /// Paths read better monospaced, and they are long enough to want the room.
    var detailFont: Font = .system(size: 11)

    var body: some View {
        HStack(spacing: 12) {
            marker
                .frame(width: 14)

            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(theme.numeral)

                Text(detail)
                    .font(detailFont)
                    .foregroundStyle(theme.dim)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer()

            if let action {
                Button(action.title, action: action.perform)
                    .buttonStyle(InstrumentButtonStyle(theme: theme, isProminent: action.isProminent))
            } else if state == .done {
                Text("Done")
                    .font(.system(size: 11))
                    .foregroundStyle(theme.dim)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 13)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(theme.surface)
        )
    }

    @ViewBuilder
    private var marker: some View {
        switch state {
        case .done:
            Image(systemName: "checkmark")
                .font(.system(size: 11, weight: .bold))
                .foregroundStyle(theme.lamp)

        case .todo:
            Circle()
                .stroke(theme.track, lineWidth: 1.6)
                .frame(width: 10, height: 10)

        case .attention:
            Circle()
                .stroke(theme.warning, lineWidth: 1.6)
                .frame(width: 10, height: 10)
        }
    }
}
