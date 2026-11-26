/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/LocalVaultCrypto.h>

#include <openssl/evp.h>

#include <memory>

namespace Superbird::Passwords {

std::optional<SecureBytes> LocalVaultCrypto::generate_dek()
{
    SecureBytes dek(KEY_LENGTH);
    if (!random_bytes(dek.span()))
        return std::nullopt;
    return dek;
}

std::optional<LocalVaultKdfParams> LocalVaultCrypto::create_default_kdf_params(
    uint32_t iterations,
    uint32_t memory_kib,
    uint32_t parallelism)
{
    LocalVaultKdfParams params;
    params.iterations = iterations;
    params.memory_kib = memory_kib;
    params.parallelism = parallelism;
    if (!random_bytes(params.salt))
        return std::nullopt;
    return params;
}

std::optional<SecureBytes> LocalVaultCrypto::derive_kek(
    std::string_view master_passphrase,
    LocalVaultKdfParams const& params)
{
    return derive_argon2id(
        { reinterpret_cast<uint8_t const*>(master_passphrase.data()), master_passphrase.size() },
        params.salt,
        params.iterations,
        params.memory_kib,
        params.parallelism,
        KEY_LENGTH);
}

std::optional<LocalVaultSealedBlob> LocalVaultCrypto::seal_aes_gcm(
    std::span<uint8_t const> key32,
    std::span<uint8_t const> plaintext,
    std::string_view aad)
{
    if (key32.size() != KEY_LENGTH)
        return std::nullopt;

    LocalVaultSealedBlob blob;
    if (!random_bytes(blob.nonce))
        return std::nullopt;

    using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        return std::nullopt;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(blob.nonce.size()), nullptr) != 1)
        return std::nullopt;
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key32.data(), blob.nonce.data()) != 1)
        return std::nullopt;

    int out_len = 0;
    if (!aad.empty()) {
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &out_len, reinterpret_cast<unsigned char const*>(aad.data()), static_cast<int>(aad.size())) != 1)
            return std::nullopt;
    }

    blob.ciphertext.resize(plaintext.size());
    int ct_len1 = 0;
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx.get(), blob.ciphertext.data(), &ct_len1, plaintext.data(), static_cast<int>(plaintext.size())) != 1)
            return std::nullopt;
    }

    int ct_len2 = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), blob.ciphertext.data() + ct_len1, &ct_len2) != 1)
        return std::nullopt;
    blob.ciphertext.resize(static_cast<size_t>(ct_len1 + ct_len2));

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(blob.tag.size()), blob.tag.data()) != 1)
        return std::nullopt;

    return blob;
}

std::optional<SecureBytes> LocalVaultCrypto::open_aes_gcm(
    std::span<uint8_t const> key32,
    LocalVaultSealedBlob const& blob,
    std::string_view aad)
{
    if (key32.size() != KEY_LENGTH)
        return std::nullopt;

    using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        return std::nullopt;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(blob.nonce.size()), nullptr) != 1)
        return std::nullopt;
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key32.data(), blob.nonce.data()) != 1)
        return std::nullopt;

    int out_len = 0;
    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &out_len, reinterpret_cast<unsigned char const*>(aad.data()), static_cast<int>(aad.size())) != 1)
            return std::nullopt;
    }

    SecureBytes plaintext(blob.ciphertext.size());
    int pt_len1 = 0;
    if (!blob.ciphertext.empty()) {
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &pt_len1, blob.ciphertext.data(), static_cast<int>(blob.ciphertext.size())) != 1)
            return std::nullopt;
    }

    auto tag_copy = blob.tag;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_copy.size()), tag_copy.data()) != 1)
        return std::nullopt;

    int pt_len2 = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + pt_len1, &pt_len2) != 1)
        return std::nullopt;

    plaintext.resize(static_cast<size_t>(pt_len1 + pt_len2));
    return plaintext;
}

std::optional<LocalVaultSealedBlob> LocalVaultCrypto::wrap_dek(
    std::span<uint8_t const> kek32,
    std::span<uint8_t const> dek32)
{
    if (dek32.size() != KEY_LENGTH)
        return std::nullopt;
    return seal_aes_gcm(kek32, dek32, DEK_WRAP_AAD);
}

std::optional<SecureBytes> LocalVaultCrypto::unwrap_dek(
    std::span<uint8_t const> kek32,
    LocalVaultSealedBlob const& wrapped)
{
    auto dek = open_aes_gcm(kek32, wrapped, DEK_WRAP_AAD);
    if (!dek || dek->size() != KEY_LENGTH)
        return std::nullopt;
    return dek;
}

std::optional<LocalVaultSealedBlob> LocalVaultCrypto::encrypt_item(
    std::span<uint8_t const> dek32,
    std::string_view item_id,
    std::string_view plaintext_json)
{
    std::string aad;
    aad.reserve(ITEM_AAD_PREFIX.size() + item_id.size());
    aad.append(ITEM_AAD_PREFIX);
    aad.append(item_id);
    return seal_aes_gcm(
        dek32,
        { reinterpret_cast<uint8_t const*>(plaintext_json.data()), plaintext_json.size() },
        aad);
}

std::optional<std::string> LocalVaultCrypto::decrypt_item(
    std::span<uint8_t const> dek32,
    std::string_view item_id,
    LocalVaultSealedBlob const& blob)
{
    std::string aad;
    aad.reserve(ITEM_AAD_PREFIX.size() + item_id.size());
    aad.append(ITEM_AAD_PREFIX);
    aad.append(item_id);
    auto bytes = open_aes_gcm(dek32, blob, aad);
    if (!bytes)
        return std::nullopt;
    return std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
}

}
