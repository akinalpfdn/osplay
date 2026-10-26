package com.akinalpfdn.osplay.storage

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import com.akinalpfdn.osplay.core.Identity
import java.security.GeneralSecurityException
import java.security.KeyStore as JavaKeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

/** Why the identity or the paired computers could not be read or stored. */
sealed interface KeyStoreError {
    data object Damaged : KeyStoreError

    data object IdentityCreationFailed : KeyStoreError

    data class System(val message: String) : KeyStoreError
}

class KeyStoreException(val error: KeyStoreError) : Exception(error.toString())

/** Where the identity and the paired computers live; a fake in tests. */
interface KeyStoring {
    /** The identity, created and stored on first use. */
    fun identity(): Identity

    fun peers(): PeerBook

    fun save(peers: PeerBook)
}

/**
 * The identity's secret key, encrypted with an AES key that never leaves the Android Keystore, and
 * the paired computers as JSON, both in app-private preferences.
 */
class AndroidKeyStore(context: Context) : KeyStoring {
    private val preferences: SharedPreferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
    private val json = Json { ignoreUnknownKeys = true }

    override fun identity(): Identity {
        val stored = preferences.getString(IDENTITY, null)
        if (stored != null) return Identity(decrypt(stored))
        val created = Identity.generate() ?: throw KeyStoreException(KeyStoreError.IdentityCreationFailed)
        preferences.edit().putString(IDENTITY, encrypt(created.secretKey)).apply()
        return created
    }

    override fun peers(): PeerBook {
        val stored = preferences.getString(PEERS, null) ?: return PeerBook()
        return try {
            json.decodeFromString<PeerBook>(stored)
        } catch (error: SerializationException) {
            throw KeyStoreException(KeyStoreError.Damaged)
        } catch (error: IllegalArgumentException) {
            throw KeyStoreException(KeyStoreError.Damaged)
        }
    }

    override fun save(peers: PeerBook) {
        preferences.edit().putString(PEERS, json.encodeToString(PeerBook.serializer(), peers)).apply()
    }

    private fun encrypt(secret: ByteArray): String = guarded {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, wrappingKey())
        val sealed = cipher.iv + cipher.doFinal(secret)
        Base64.encodeToString(sealed, Base64.NO_WRAP)
    }

    private fun decrypt(stored: String): ByteArray = guarded {
        val sealed = Base64.decode(stored, Base64.NO_WRAP)
        if (sealed.size <= IV_BYTES) throw KeyStoreException(KeyStoreError.Damaged)
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, wrappingKey(), GCMParameterSpec(TAG_BITS, sealed, 0, IV_BYTES))
        cipher.doFinal(sealed, IV_BYTES, sealed.size - IV_BYTES)
    }

    private fun wrappingKey(): SecretKey {
        val keyStore = JavaKeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(KEY_BITS)
                .build(),
        )
        return generator.generateKey()
    }

    private inline fun <T> guarded(block: () -> T): T = try {
        block()
    } catch (error: GeneralSecurityException) {
        throw KeyStoreException(KeyStoreError.System(error.javaClass.simpleName))
    } catch (error: IllegalArgumentException) {
        throw KeyStoreException(KeyStoreError.Damaged)
    }

    private companion object {
        const val PREFERENCES = "osplay.keys"
        const val IDENTITY = "identity"
        const val PEERS = "paired-devices"
        const val ANDROID_KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "com.akinalpfdn.osplay.identity"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        const val IV_BYTES = 12
        const val TAG_BITS = 128
        const val KEY_BITS = 256
    }
}
