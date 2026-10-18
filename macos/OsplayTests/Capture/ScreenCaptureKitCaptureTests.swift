import CoreMedia
import Foundation
import ScreenCaptureKit
import Testing

@testable import Osplay

@Suite("ScreenCaptureKit capture")
struct ScreenCaptureKitCaptureTests {
    @Test("The stream is configured for the encoder at the stream's pixel size")
    func streamConfiguration() {
        let configuration = ScreenCaptureKitCapture.streamConfiguration(
            CaptureConfiguration(displayID: 7, width: 2560, height: 1440, frameRate: 60, showsCursor: true))
        #expect(configuration.width == 2560)
        #expect(configuration.height == 1440)
        #expect(configuration.pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
        #expect(configuration.showsCursor)
        #expect(
            abs(configuration.minimumFrameInterval.seconds - 1.0 / (60 * Tunables.Capture.frameRateHeadroom)) < 0.000_01
        )
        #expect(configuration.queueDepth == Tunables.Capture.queueDepth)
    }

    @Test(
        "Stream errors map to reasons the UI can explain",
        arguments: [
            (SCStreamError.Code.userDeclined.rawValue, ScreenCaptureError.permissionDenied),
            (SCStreamError.Code.userStopped.rawValue, .stoppedByUser),
            (ScreenCaptureError.systemStoppedStreamCode, .stoppedBySystem),
            (SCStreamError.Code.noCaptureSource.rawValue, .displayNotFound),
            (SCStreamError.Code.internalError.rawValue, .failed(code: SCStreamError.Code.internalError.rawValue)),
        ])
    func mapsStreamErrors(_ code: Int, _ expected: ScreenCaptureError) {
        #expect(ScreenCaptureError(NSError(domain: SCStreamErrorDomain, code: code)) == expected)
    }

    @Test("Errors from other domains keep their code")
    func mapsForeignErrors() {
        #expect(ScreenCaptureError(NSError(domain: NSPOSIXErrorDomain, code: 5)) == .failed(code: 5))
    }
}
