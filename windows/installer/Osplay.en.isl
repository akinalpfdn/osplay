; Osplay's own installer messages (English). Inno Setup reads .isl files as UTF-8 only with a BOM.

[CustomMessages]
DriverSetupFailed=The Virtual Display Driver could not be installed (error %1). Osplay can still be used as a display, but it cannot extend this PC. Run this installer again to retry.
HelperSetupFailed=The Osplay helper service could not be installed (error %1). Osplay can still be used as a display, but it cannot extend this PC. Run this installer again to retry.
FirewallSetupFailed=The firewall rule for Osplay could not be added (error %1). Windows may ask whether Osplay may use the network when it first starts.
UsbDkSetupFailed=UsbDk could not be installed (error %1). Osplay works over Wi-Fi, but it cannot use Android devices on the USB cable. Run this installer again to retry.
UsbDkRestartNote=Android phones and tablets on the USB cable work after Windows restarts once. Everything else works now.
UsbDkRemovalPendingNote=To use Android phones and tablets on the USB cable, restart Windows and run this installer again: the USB driver removed earlier goes only at the restart. Everything else works now.
AccessoryDriverSetupFailed=The USB driver for Android devices could not be installed (error %1). Osplay works over Wi-Fi, but it cannot use Android devices on the USB cable. The details are in %ProgramData%\Osplay\logs\accessory-driver.log. Run this installer again to retry.
RemoveDriverQuestion=Also remove the Virtual Display Driver that Osplay installed?%n%nChoose No if another app uses it.
