import SwiftUI

/// The old System Settings Arrange view, for one paired device: this Mac's main display and that
/// device's monitors as boxes; drag a monitor and drop it next to another display.
struct ArrangementCanvas: View {
    let main: CGSize
    let monitors: [ArrangedMonitor]
    let move: (UInt8, CGPoint) -> Void
    @State private var drag: (id: UInt8, translation: CGSize)?

    var body: some View {
        GeometryReader { geometry in
            let bounds = Self.bounds(main: main, monitors: monitors)
            let scale = min(geometry.size.width / bounds.width, geometry.size.height / bounds.height)
            ZStack(alignment: .topLeading) {
                DisplayBox(title: Text("settings.arrange.thisMac"), isMain: true)
                    .frame(width: main.width * scale, height: main.height * scale)
                    .offset(x: -bounds.minX * scale, y: -bounds.minY * scale)
                ForEach(monitors) { monitor in
                    let translation = drag?.id == monitor.id ? drag?.translation ?? .zero : .zero
                    DisplayBox(title: Text(verbatim: monitor.name), isMain: false)
                        .frame(width: monitor.size.width * scale, height: monitor.size.height * scale)
                        .offset(
                            x: (monitor.origin.x - bounds.minX) * scale + translation.width,
                            y: (monitor.origin.y - bounds.minY) * scale + translation.height
                        )
                        .gesture(
                            DragGesture()
                                .onChanged { value in drag = (monitor.id, value.translation) }
                                .onEnded { value in
                                    drag = nil
                                    move(
                                        monitor.id,
                                        CGPoint(
                                            x: monitor.origin.x + value.translation.width / scale,
                                            y: monitor.origin.y + value.translation.height / scale))
                                }
                        )
                        .accessibilityLabel(Text(verbatim: monitor.name))
                        .accessibilityHint(Text("settings.arrange.hint"))
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height, alignment: .topLeading)
        }
        .frame(height: Theme.Arrangement.canvasHeight)
    }

    /// Everything plus a margin, so a display can be dragged a little beyond the others.
    private static func bounds(main: CGSize, monitors: [ArrangedMonitor]) -> CGRect {
        let all = monitors.map(\.frame).reduce(CGRect(origin: .zero, size: main)) { $0.union($1) }
        let margin = max(all.width, all.height) * Theme.Arrangement.marginFraction
        return all.insetBy(dx: -margin, dy: -margin)
    }
}

private struct DisplayBox: View {
    let title: Text
    let isMain: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: Theme.Arrangement.cornerRadius)
            .fill(isMain ? Theme.Arrangement.mainFill : Theme.Arrangement.monitorFill)
            .overlay(
                RoundedRectangle(cornerRadius: Theme.Arrangement.cornerRadius)
                    .strokeBorder(Theme.Arrangement.border)
            )
            .overlay(
                title
                    .font(Theme.Typography.caption)
                    .multilineTextAlignment(.center)
                    .padding(Theme.Spacing.small)
            )
    }
}
