package com.akinalpfdn.osplay.ui

import androidx.compose.foundation.border
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Shape
import com.akinalpfdn.osplay.ui.theme.Sizes

/**
 * A ring around an element while it has focus. On a TV the remote moves focus, and Material's own
 * focus tint is too faint to see from across a room; with touch nothing has focus, so phones never
 * show it. Put it before the element's clickable.
 */
fun Modifier.focusRing(shape: Shape): Modifier = composed {
    var focused by remember { mutableStateOf(false) }
    val color = MaterialTheme.colorScheme.primary
    onFocusChanged { focused = it.isFocused }
        .then(if (focused) Modifier.border(Sizes.FocusRing, color, shape) else Modifier)
}
