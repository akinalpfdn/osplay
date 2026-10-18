/// Composition root: the single place where concrete services are created and injected.
@MainActor
final class AppDependencies {
    let virtualDisplays: any VirtualDisplayProviding
    let captures: any ScreenCaptureMaking
    let encoders: any VideoEncoderMaking
    let permissions: any PermissionChecking

    init(
        virtualDisplays: any VirtualDisplayProviding = CGVirtualDisplayProvider(),
        captures: any ScreenCaptureMaking = ScreenCaptureKitCaptureFactory(),
        encoders: any VideoEncoderMaking = VideoToolboxEncoderFactory(),
        permissions: any PermissionChecking = SystemPermissions()
    ) {
        self.virtualDisplays = virtualDisplays
        self.captures = captures
        self.encoders = encoders
        self.permissions = permissions
    }
}
