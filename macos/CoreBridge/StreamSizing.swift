import OsplayCore

/// The size a host streams a monitor at (DEVPLAN Phase 13c), as both hosts compute it.
public enum StreamSizing {
    /// The monitor fitted inside `box` with its aspect ratio kept (the longer side against the box's
    /// longer side), never larger than the monitor or than a stream may be, both sides even. A nil
    /// box keeps the monitor's own size (within the protocol's limit).
    public static func fit(width: Int, height: Int, inside box: (width: Int, height: Int)?) -> (width: Int, height: Int)
    {
        var fittedWidth: UInt32 = 0
        var fittedHeight: UInt32 = 0
        let result = osplay_fit_stream_size(
            UInt32(clamping: width), UInt32(clamping: height), UInt32(clamping: box?.width ?? 0),
            UInt32(clamping: box?.height ?? 0), &fittedWidth, &fittedHeight)
        guard result == OSPLAY_OK else { return (width, height) }
        return (Int(fittedWidth), Int(fittedHeight))
    }
}
