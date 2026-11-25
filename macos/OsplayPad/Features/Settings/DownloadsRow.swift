import CoreImage.CIFilterBuiltins
import SwiftUI

/// Where to get Osplay for the Mac or PC this device shows: a link on iPad; on Apple TV, which
/// cannot open links, a QR code and a short address.
struct DownloadsRow: View {
    var body: some View {
        #if os(tvOS)
            HStack(spacing: Theme.Spacing.large) {
                if let code = Self.qrCode(for: Tunables.Pad.downloadsAddress) {
                    code
                        .interpolation(.none)
                        .resizable()
                        .frame(width: Theme.Size.downloadsCode, height: Theme.Size.downloadsCode)
                        .padding(Theme.DownloadsCode.quietZone)
                        .background(Theme.DownloadsCode.background)
                        .accessibilityLabel(Text("pad.settings.downloadsCode"))
                }
                Text(verbatim: Tunables.Pad.downloadsShortAddress)
                    .font(Theme.Typography.body)
            }
            .focusable()  // lets the remote scroll past the code to the rows below
        #else
            if let url = URL(string: Tunables.Pad.downloadsAddress) {
                Link("pad.settings.downloadsLink", destination: url)
            }
        #endif
    }

    #if os(tvOS)
        /// The address as a QR code, one pixel per module; scaled up without smoothing when drawn.
        private static func qrCode(for address: String) -> Image? {
            let filter = CIFilter.qrCodeGenerator()
            filter.message = Data(address.utf8)
            filter.correctionLevel = "M"
            guard let output = filter.outputImage,
                let image = CIContext().createCGImage(output, from: output.extent)
            else { return nil }
            return Image(decorative: image, scale: 1)
        }
    #endif
}
