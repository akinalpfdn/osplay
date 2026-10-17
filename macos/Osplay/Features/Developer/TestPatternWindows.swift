#if DEBUG
    import AppKit
    import SwiftUI

    /// Fills displays with a moving test pattern so captures produce a frame on every refresh.
    @MainActor
    final class TestPatternWindows {
        private var windows: [NSWindow] = []

        func show(on displayIDs: [CGDirectDisplayID]) {
            close()
            for displayID in displayIDs {
                guard let screen = NSScreen.screens.first(where: { $0.displayID == displayID }) else { continue }
                let window = NSWindow(
                    contentRect: screen.frame, styleMask: .borderless, backing: .buffered, defer: false, screen: screen)
                window.isReleasedWhenClosed = false
                window.contentView = NSHostingView(rootView: TestPatternView())
                window.setFrame(screen.frame, display: true)
                window.orderFrontRegardless()
                windows.append(window)
            }
        }

        func close() {
            windows.forEach { $0.close() }
            windows.removeAll()
        }
    }

    private struct TestPatternView: View {
        var body: some View {
            TimelineView(.animation) { context in
                let seconds = context.date.timeIntervalSinceReferenceDate
                Canvas { canvas, size in
                    canvas.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Theme.TestPattern.background))
                    let sweep = Theme.TestPattern.sweepDuration
                    let progress = seconds.truncatingRemainder(dividingBy: sweep) / sweep
                    let barWidth = size.width * Theme.TestPattern.barWidthFraction
                    let bar = CGRect(x: (size.width - barWidth) * progress, y: 0, width: barWidth, height: size.height)
                    canvas.fill(Path(bar), with: .color(Theme.TestPattern.bar))
                    canvas.draw(
                        Text(verbatim: String(format: "%.3f", seconds.truncatingRemainder(dividingBy: 1000)))
                            .font(Theme.TestPattern.counterFont)
                            .foregroundStyle(Theme.TestPattern.accent),
                        at: CGPoint(x: size.width / 2, y: size.height / 2))
                }
            }
        }
    }

    extension NSScreen {
        var displayID: CGDirectDisplayID? {
            (deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value
        }
    }
#endif
