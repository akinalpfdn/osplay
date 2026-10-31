# R8 rules for the release build. Libraries bring their own (kotlinx.serialization, Compose,
# AndroidX); these cover what only the JNI bridge knows about.

# osplay_jni.cpp finds the listener's callbacks by name and signature (find_methods) on the
# object Kotlin passes in, so neither the interface's methods nor its implementations' may be
# renamed or removed. Native's external functions are kept by the default rules.
-keep interface com.akinalpfdn.osplay.core.SessionListener { *; }
-keepclassmembers class * implements com.akinalpfdn.osplay.core.SessionListener {
    public <methods>;
}
