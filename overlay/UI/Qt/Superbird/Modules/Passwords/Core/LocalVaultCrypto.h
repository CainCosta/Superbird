/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

struct LocalVaultKdfParams {
    uint32_t iterations { 3 };
    uint32_t memory_kib { 65536 }; // 64 MiB
    uint32_t parallelism { 4 };
    std::array<uint8_t, 16> salt {};
};

struct LocalVaultSealedBlob {
    std::array<uint8_t, 12> nonce {};
    std::vector<uint8_t> ciphertext;
    std::array<uint8_t, 16> tag {};
};

class LocalVaultCrypto {
public:
    static constexpr size_t KEY_LENGTH = 32;
    static constexpr std::string_view DEK_WRAP_AAD = "superbird-vault-dek-v1";
    static constexpr std::string_view ITEM_AAD_PREFIX = "superbird-vault-v1|";

    static std::optional<SecureBytes> generate_dek();
    static std::optional<LocalVaultKdfParams> create_default_kdf_params(
        uint32_t iterations = 3,
        uint32_t memory_kib = 65536,
        uint32_t parallelism = 4);

    static std::optional<SecureBytes> derive_kek(
        std::string_view master_passphrase,
        LocalVaultKdfParams const& params);

    static std::optional<LocalVaultSealedBlob> seal_aes_gcm(
        std::span<uint8_t const> key32,
        std::span<uint8_t const> plaintext,
        std::string_view aad);

    static std::optional<SecureBytes> open_aes_gcm(
        std::span<uint8_t const> key32,
        LocalVaultSealedBlob const& blob,
        std::string_view aad);

    static std::optional<LocalVaultSealedBlob> wrap_dek(
        std::span<uint8_t const> kek32,
        std::span<uint8_t const> dek32);

    static std::optional<SecureBytes> unwrap_dek(
        std::span<uint8_t const> kek32,
        LocalVaultSealedBlob const& wrapped);

    static std::optional<LocalVaultSealedBlob> encrypt_item(
        std::span<uint8_t const> dek32,
        std::string_view item_id,
        std::string_view plaintext_json);

    static std::optional<std::string> decrypt_item(
        std::span<uint8_t const> dek32,
        std::string_view item_id,
        LocalVaultSealedBlob const& blob);
};

}
