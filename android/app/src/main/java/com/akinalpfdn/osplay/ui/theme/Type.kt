package com.akinalpfdn.osplay.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

val OsplayTypography = Typography()

/** Styles that Material's scale does not have. */
object OsplayText {
    /** The six pairing digits: large and evenly spaced so both screens are easy to compare. */
    val PairingCode = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 48.sp)
    val Stats = TextStyle(fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Medium, fontSize = 13.sp)
}
