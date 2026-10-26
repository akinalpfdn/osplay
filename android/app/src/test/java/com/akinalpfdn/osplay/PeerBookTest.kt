package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.core.Trust
import com.akinalpfdn.osplay.storage.PeerBook
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PeerBookTest {
    private val key = "ab".repeat(32)

    @Test
    fun `should ask for a pairing code when the computer is unknown`() {
        assertEquals(Trust.Unknown, PeerBook().trust(key))
    }

    @Test
    fun `should trust a paired computer and refuse it once revoked`() {
        val paired = PeerBook().paired(key, "PC", nowMillis = 1)
        assertEquals(Trust.Trusted, paired.trust(key))
        assertEquals(Trust.Blocked, paired.revoke(key).trust(key))
    }

    @Test
    fun `should ask for a code again when a computer is removed`() {
        val book = PeerBook().paired(key, "PC", nowMillis = 1).revoke(key).remove(key)
        assertTrue(book.peers.isEmpty())
        assertEquals(Trust.Unknown, book.trust(key))
    }

    @Test
    fun `should replace a computer that pairs again`() {
        val book = PeerBook().paired(key, "Old", 1).revoke(key).paired(key, "New", 2)
        assertEquals(1, book.peers.size)
        assertEquals("New", book.peers.single().name)
        assertEquals(Trust.Trusted, book.trust(key))
    }
}
