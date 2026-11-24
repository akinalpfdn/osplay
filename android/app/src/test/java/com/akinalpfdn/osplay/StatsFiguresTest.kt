package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.viewer.SoundFigures
import com.akinalpfdn.osplay.viewer.StatsFigures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StatsFiguresTest {
    private val stream = intArrayOf(0, 60, 22_100, 3, 12_400)

    @Test
    fun `should read the stream figures without a sound line when no sound plays`() {
        val figures = StatsFigures.from(rttUs = 4_200, streams = stream, audio = intArrayOf())

        assertEquals(StatsFigures(60, 22_100, 4, 0.3, 12, sound = null), figures)
    }

    @Test
    fun `should add the sound's reserve, latency and gaps while sound plays`() {
        val figures = StatsFigures.from(rttUs = 4_200, streams = stream, audio = intArrayOf(41_000, 0, 2, 7))

        assertEquals(SoundFigures(reserveMilliseconds = 41, latencyMilliseconds = null, gaps = 2, concealedFrames = 7), figures?.sound)
    }

    @Test
    fun `should report nothing when the core sent no stream`() {
        assertNull(StatsFigures.from(rttUs = 4_200, streams = intArrayOf(), audio = intArrayOf(41_000, 55_000, 0, 0)))
    }
}
