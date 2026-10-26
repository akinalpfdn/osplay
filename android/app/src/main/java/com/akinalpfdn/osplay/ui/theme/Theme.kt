package com.akinalpfdn.osplay.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

private val Light = lightColorScheme(primary = OsplayColors.Brand)
private val Dark = darkColorScheme(primary = OsplayColors.BrandLight)

/** Colors outside Material's scheme that follow light and dark. */
data class ExtraColors(val warning: Color)

val LocalExtraColors = staticCompositionLocalOf { ExtraColors(warning = OsplayColors.Warning) }

@Composable
fun OsplayTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    CompositionLocalProvider(
        LocalExtraColors provides ExtraColors(warning = if (dark) OsplayColors.WarningDark else OsplayColors.Warning),
    ) {
        MaterialTheme(colorScheme = if (dark) Dark else Light, typography = OsplayTypography, content = content)
    }
}
