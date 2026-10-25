import Foundation

/// The virtual displays every host session of this Mac draws from: a new viewer gets what is left
/// (DECISIONS.md, 2026-10-25). `@unchecked`: `used` is guarded by `lock`.
final class DisplayBudget: @unchecked Sendable {
    private let lock = NSLock()
    private let total: Int
    private var used = 0

    init(total: Int = Tunables.VirtualDisplay.maximumTotalCount) {
        self.total = total
    }

    /// Takes as many as are left, up to `count`; 0 when none is.
    func take(upTo count: Int) -> Int {
        lock.withLock {
            let granted = min(max(count, 0), total - used)
            used += granted
            return granted
        }
    }

    func giveBack(_ count: Int) {
        lock.withLock { used = max(used - count, 0) }
    }

    var isExhausted: Bool { lock.withLock { used >= total } }
}
