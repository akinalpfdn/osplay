#if DEBUG
    import AppKit
    import QuartzCore

    /// Fills displays with a moving test pattern so captures produce a frame on every refresh.
    /// Core Animation runs the motion in the render server, so a busy main thread cannot make the
    /// pattern itself stutter and distort the measurement.
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
                window.contentView = Self.patternView(size: screen.frame.size)
                window.setFrame(screen.frame, display: true)
                window.orderFrontRegardless()
                windows.append(window)
            }
        }

        func close() {
            windows.forEach { $0.close() }
            windows.removeAll()
        }

        private static func patternView(size: CGSize) -> NSView {
            let view = NSView(frame: CGRect(origin: .zero, size: size))
            view.wantsLayer = true
            guard let root = view.layer else { return view }
            root.backgroundColor = NSColor(Theme.TestPattern.background).cgColor

            let barWidth = size.width * Theme.TestPattern.barWidthFraction
            let bar = CALayer()
            bar.backgroundColor = NSColor(Theme.TestPattern.bar).cgColor
            bar.bounds = CGRect(x: 0, y: 0, width: barWidth, height: size.height)
            bar.position = CGPoint(x: barWidth / 2, y: size.height / 2)
            root.addSublayer(bar)

            let sweep = CABasicAnimation(keyPath: "position.x")
            sweep.fromValue = barWidth / 2
            sweep.toValue = size.width - barWidth / 2
            sweep.duration = Theme.TestPattern.sweepDuration
            sweep.repeatCount = .infinity
            bar.add(sweep, forKey: "sweep")

            // A second, vertical motion so every frame differs across the whole display.
            let marker = CALayer()
            marker.backgroundColor = NSColor(Theme.TestPattern.accent).cgColor
            marker.bounds = CGRect(
                x: 0, y: 0, width: size.width, height: size.height * Theme.TestPattern.barWidthFraction)
            marker.position = CGPoint(x: size.width / 2, y: 0)
            root.addSublayer(marker)

            let fall = CABasicAnimation(keyPath: "position.y")
            fall.fromValue = 0
            fall.toValue = size.height
            fall.duration = Theme.TestPattern.sweepDuration * 1.5
            fall.repeatCount = .infinity
            marker.add(fall, forKey: "fall")
            return view
        }
    }
#endif
