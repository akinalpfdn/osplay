package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.discovery.DnsSdMessage
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DnsSdMessageTest {
    private val type = "_osplay._tcp"
    private val instance = "21dea6d39a2fb2ed47d6bfa45d7be95e"

    /** A response shaped like Windows' answer on the PC's network: one PTR, the rest additional. */
    private fun windowsAnswer(ptrTtl: Long = 4500, withAddress: Boolean = true): ByteArray {
        val out = Message()
        out.u16(0); out.u16(0x8400); out.u16(0); out.u16(1); out.u16(0); out.u16(if (withAddress) 4 else 3)
        val typeAt = out.size
        out.name("_osplay._tcp.local")
        out.u16(12); out.u16(1); out.u32(ptrTtl)
        val instanceRdata = Message().apply { label(instance); pointer(typeAt) }.bytes()
        out.u16(instanceRdata.size); val instanceAt = out.size; out.raw(instanceRdata)
        // SRV, cache-flush class
        out.pointer(instanceAt); out.u16(33); out.u16(0x8001); out.u32(120)
        val srv = Message().apply { u16(0); u16(0); u16(51903); label("akinalp"); name("local") }.bytes()
        out.u16(srv.size); val targetAt = out.size + 6; out.raw(srv)
        // TXT
        out.pointer(instanceAt); out.u16(16); out.u16(0x8001); out.u32(4500)
        val txt = Message().apply {
            for (entry in listOf("pv=1", "name=akinalp", "id=$instance", "roles=1", "os=windows", "pm=2", "flag")) {
                val bytes = entry.encodeToByteArray(); u8(bytes.size); raw(bytes)
            }
        }.bytes()
        out.u16(txt.size); out.raw(txt)
        if (withAddress) {
            out.pointer(targetAt); out.u16(1); out.u16(0x8001); out.u32(120); out.u16(4)
            out.raw(byteArrayOf(192.toByte(), 168.toByte(), 137.toByte(), 1))
        }
        out.pointer(targetAt); out.u16(28); out.u16(0x8001); out.u32(120); out.u16(16); out.raw(ByteArray(16))
        return out.bytes()
    }

    @Test
    fun `should read a computer from a Windows answer`() {
        val bytes = windowsAnswer()
        val answer = DnsSdMessage.parse(bytes, bytes.size, type)!!
        val service = answer.services.single()
        assertEquals(instance, service.instance)
        assertEquals(51903, service.port)
        assertEquals("192.168.137.1", service.ipv4)
        assertArrayEquals("windows".encodeToByteArray(), service.txt["os"])
        assertArrayEquals(ByteArray(0), service.txt["flag"])
    }

    @Test
    fun `should report a goodbye when the record's TTL is zero`() {
        val bytes = windowsAnswer(ptrTtl = 0)
        val answer = DnsSdMessage.parse(bytes, bytes.size, type)!!
        assertTrue(answer.services.isEmpty())
        assertEquals(listOf(instance), answer.goodbyes)
    }

    @Test
    fun `should skip a computer whose address is missing`() {
        val bytes = windowsAnswer(withAddress = false)
        assertTrue(DnsSdMessage.parse(bytes, bytes.size, type)!!.services.isEmpty())
    }

    @Test
    fun `should ignore a question`() {
        val query = DnsSdMessage.query(type)
        assertNull(DnsSdMessage.parse(query, query.size, type))
    }

    @Test
    fun `should reject a message cut short at any length`() {
        val bytes = windowsAnswer()
        for (length in 0 until bytes.size - 1) {
            val answer = DnsSdMessage.parse(bytes, length, type)
            assertTrue("length $length", answer == null || answer.services.isEmpty())
        }
    }

    @Test
    fun `should reject a name pointer that loops`() {
        val out = Message()
        out.u16(0); out.u16(0x8400); out.u16(0); out.u16(1); out.u16(0); out.u16(0)
        out.pointer(12) // points at itself
        out.u16(12); out.u16(1); out.u32(120); out.u16(0)
        val bytes = out.bytes()
        assertNull(DnsSdMessage.parse(bytes, bytes.size, type))
    }

    @Test
    fun `should ask for the service type's PTR record`() {
        val query = DnsSdMessage.query(type)
        val expectedName = byteArrayOf(7) + "_osplay".encodeToByteArray() + byteArrayOf(4) + "_tcp".encodeToByteArray() +
            byteArrayOf(5) + "local".encodeToByteArray() + byteArrayOf(0)
        assertArrayEquals(byteArrayOf(0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0) + expectedName + byteArrayOf(0, 12, 0, 1), query)
    }

    private class Message {
        private val out = ByteArrayOutputStream()
        val size: Int get() = out.size()

        fun u8(value: Int) = out.write(value)
        fun u16(value: Int) { out.write(value ushr 8 and 0xFF); out.write(value and 0xFF) }
        fun u32(value: Long) { u16((value ushr 16).toInt()); u16((value and 0xFFFF).toInt()) }
        fun raw(bytes: ByteArray) = out.write(bytes)
        fun label(text: String) { val bytes = text.encodeToByteArray(); u8(bytes.size); raw(bytes) }
        fun name(text: String) { text.split('.').forEach(::label); u8(0) }
        fun pointer(offset: Int) = u16(0xC000 or offset)
        fun bytes(): ByteArray = out.toByteArray()
    }
}
