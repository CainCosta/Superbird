/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

// RAII buffer that wipes its contents via OPENSSL_cleanse on destruction.
class SecureBytes {
public:
    SecureBytes() = default;
    explicit SecureBytes(size_t size);
    explicit SecureBytes(std::span<uint8_t const> data);
    explicit SecureBytes(std::vector<uint8_t>&& data);
    ~SecureBytes();

    SecureBytes(SecureBytes const& other);
    SecureBytes& operator=(SecureBytes const& other);
    SecureBytes(SecureBytes&& other) noexcept;
    SecureBytes& operator=(SecureBytes&& other) noexcept;

    void clear();
    void resize(size_t size);

    uint8_t* data() { return m_data.data(); }
    uint8_t const* data() const { return m_data.data(); }
    size_t size() const { return m_data.size(); }
    bool empty() const { return m_data.empty(); }

    std::span<uint8_t> span() { return m_data; }
    std::span<uint8_t const> span() const { return m_data; }

private:
    std::vector<uint8_t> m_data;
};

void cleanse_memory(void* ptr, size_t len);
void cleanse_string(std::string& str);

// Constant-time comparison of two byte spans.
bool constant_time_equals(std::span<uint8_t const> a, std::span<uint8_t const> b);

// Base64 / Base64URL helpers.
std::string base64_encode(std::span<uint8_t const> data);
std::optional<std::vector<uint8_t>> base64_decode(std::string_view encoded);
std::string base64url_encode(std::span<uint8_t const> data);

// Cryptographically secure random bytes.
bool random_bytes(std::span<uint8_t> out);

// RFC 9106 Argon2id (uses OpenSSL 3.2+ EVP_KDF "ARGON2ID" when available, with a
// clean-room RFC 9106 fallback when linked against OpenSSL 3.0.x in standalone unit tests).
std::optional<SecureBytes> derive_argon2id(
    std::span<uint8_t const> password,
    std::span<uint8_t const> salt,
    uint32_t iterations,
    uint32_t memory_kib,
    uint32_t parallelism,
    size_t output_len);

enum class BitwardenKdfType : uint8_t {
    Pbkdf2Sha256 = 0,
    Argon2id = 1,
};

struct BitwardenKdfConfig {
    BitwardenKdfType type { BitwardenKdfType::Pbkdf2Sha256 };
    uint32_t iterations { 600000 };
    uint32_t memory_mb { 64 };
    uint32_t parallelism { 4 };
};

struct BitwardenSymmetricKey {
    SecureBytes enc_key; // 32 bytes (AES-256)
    SecureBytes mac_key; // 32 bytes (HMAC-SHA256)

    bool is_valid() const { return enc_key.size() == 32 && mac_key.size() == 32; }
    void clear()
    {
        enc_key.clear();
        mac_key.clear();
    }

    static std::optional<BitwardenSymmetricKey> from_bytes(std::span<uint8_t const> bytes64);
    SecureBytes to_bytes() const;
};

enum class BitwardenCipherType : uint8_t {
    AesCbc256_B64 = 0,
    AesCbc256_HmacSha256_B64 = 2,
    Rsa2048_OaepSha256_B64 = 3,
    Rsa2048_OaepSha1_B64 = 4,
    Rsa2048_OaepSha256_HmacSha256_B64 = 5,
    Rsa2048_OaepSha1_HmacSha256_B64 = 6,
};

struct BitwardenCipherString {
    BitwardenCipherType type { BitwardenCipherType::AesCbc256_HmacSha256_B64 };
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> mac;
};

class BitwardenCrypto {
public:
    static std::string normalize_email(std::string_view email);
    static std::string auth_email_header(std::string_view email);

    static std::optional<SecureBytes> derive_master_key(
        std::string_view password,
        std::string_view email,
        BitwardenKdfConfig const& config);

    static std::optional<std::string> derive_master_password_hash(
        std::span<uint8_t const> master_key,
        std::string_view password);

    static std::optional<BitwardenSymmetricKey> stretch_master_key(
        std::span<uint8_t const> master_key);

    static std::optional<SecureBytes> hkdf_expand_sha256(
        std::span<uint8_t const> prk,
        std::string_view info,
        size_t output_len);

    static std::optional<BitwardenCipherString> parse_cipher_string(std::string_view cipher_string);

    // Constant-time MAC verification before decryption.
    static std::optional<SecureBytes> decrypt_bytes(
        std::string_view cipher_string,
        BitwardenSymmetricKey const& key);

    static std::optional<std::string> decrypt_string(
        std::string_view cipher_string,
        BitwardenSymmetricKey const& key);

    // Encrypts as Type 2: `2.<b64_iv>|<b64_ct>|<b64_mac>`.
    static std::optional<std::string> encrypt_bytes(
        std::span<uint8_t const> plaintext,
        BitwardenSymmetricKey const& key,
        std::optional<std::array<uint8_t, 16>> deterministic_iv = std::nullopt);

    static std::optional<std::string> encrypt_string(
        std::string_view plaintext,
        BitwardenSymmetricKey const& key);

    // Unwrap Type 3/4/5/6 RSA-OAEP encrypted key (e.g., organization vault key) using PKCS#8 DER private key.
    static std::optional<SecureBytes> rsa_decrypt(
        std::string_view cipher_string,
        std::span<uint8_t const> pkcs8_private_key_der);

    // Encrypt bytes with RSA-OAEP (Type 3 = SHA-256, Type 4 = SHA-1) using SPKI DER public key (useful for tests & org sharing).
    static std::optional<std::string> rsa_encrypt(
        std::span<uint8_t const> plaintext,
        std::span<uint8_t const> spki_public_key_der,
        BitwardenCipherType type = BitwardenCipherType::Rsa2048_OaepSha1_B64);

    static std::optional<BitwardenSymmetricKey> generate_symmetric_key();
};

}
