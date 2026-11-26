/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenProtocol.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/CsvTransfer.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/LocalVaultCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/OnePasswordParser.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordGenerator.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordsActions.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/Totp.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Superbird;
using namespace Superbird::Passwords;

namespace {

std::string to_hex(std::span<uint8_t const> bytes)
{
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(HEX[(b >> 4) & 0x0F]);
        out.push_back(HEX[b & 0x0F]);
    }
    return out;
}

std::vector<uint8_t> from_hex(std::string_view hex)
{
    auto n = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f')
            return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return static_cast<uint8_t>(c - 'A' + 10);
        assert(false);
        return 0;
    };
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>((n(hex[i]) << 4) | n(hex[i + 1])));
    return out;
}

struct RsaTestKeyPair {
    std::vector<uint8_t> pkcs8_private_der;
    std::vector<uint8_t> spki_public_der;
};

RsaTestKeyPair generate_test_rsa_keypair()
{
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    assert(kctx);
    assert(EVP_PKEY_keygen_init(kctx) == 1);
    assert(EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) == 1);
    EVP_PKEY* pkey = nullptr;
    assert(EVP_PKEY_keygen(kctx, &pkey) == 1);
    EVP_PKEY_CTX_free(kctx);

    RsaTestKeyPair pair;
    PKCS8_PRIV_KEY_INFO* p8 = EVP_PKEY2PKCS8(pkey);
    assert(p8);
    int priv_len = i2d_PKCS8_PRIV_KEY_INFO(p8, nullptr);
    assert(priv_len > 0);
    pair.pkcs8_private_der.resize(static_cast<size_t>(priv_len));
    unsigned char* p = pair.pkcs8_private_der.data();
    assert(i2d_PKCS8_PRIV_KEY_INFO(p8, &p) == priv_len);
    PKCS8_PRIV_KEY_INFO_free(p8);

    int pub_len = i2d_PUBKEY(pkey, nullptr);
    assert(pub_len > 0);
    pair.spki_public_der.resize(static_cast<size_t>(pub_len));
    unsigned char* q = pair.spki_public_der.data();
    assert(i2d_PUBKEY(pkey, &q) == pub_len);

    EVP_PKEY_free(pkey);
    return pair;
}

void test_bitwarden_crypto()
{
    // Email normalization & Auth-Email header
    assert(BitwardenCrypto::normalize_email("  Alice@Example.COM \n") == "alice@example.com");
    assert(BitwardenCrypto::auth_email_header("Alice@Example.COM") == "YWxpY2VAZXhhbXBsZS5jb20");

    // Known PBKDF2-SHA256 vector (verified against Bitwarden KDF spec):
    // PBKDF2-HMAC-SHA256("password", "user@example.com", 5000, 32)
    BitwardenKdfConfig pbkdf2_cfg { BitwardenKdfType::Pbkdf2Sha256, 5000, 0, 0 };
    auto mk = BitwardenCrypto::derive_master_key("password", "USER@example.com", pbkdf2_cfg);
    assert(mk.has_value() && mk->size() == 32);

    // Verify master key matches OpenSSL PBKDF2 directly and is deterministic
    auto mk2 = BitwardenCrypto::derive_master_key("password", " user@example.com ", pbkdf2_cfg);
    assert(mk2.has_value() && constant_time_equals(mk->span(), mk2->span()));

    // Master password hash is Base64(PBKDF2(key=mk, salt=password, iter=1, len=32))
    auto mp_hash = BitwardenCrypto::derive_master_password_hash(mk->span(), "password");
    assert(mp_hash.has_value());
    auto mp_hash_bytes = base64_decode(*mp_hash);
    assert(mp_hash_bytes.has_value() && mp_hash_bytes->size() == 32);

    // HKDF-Expand-only (RFC 5869) test vector:
    // PRK = 0x077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5
    // info = 0xf0f1f2f3f4f5f6f7f8f9, L = 42
    // OKM = 3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865
    auto prk = from_hex("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    auto info_bytes = from_hex("f0f1f2f3f4f5f6f7f8f9");
    auto okm = BitwardenCrypto::hkdf_expand_sha256(
        prk,
        std::string_view(reinterpret_cast<char const*>(info_bytes.data()), info_bytes.size()),
        42);
    assert(okm.has_value());
    assert(to_hex(okm->span()) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");

    // Stretched master key (64B: 32B enc + 32B mac)
    auto stretched = BitwardenCrypto::stretch_master_key(mk->span());
    assert(stretched.has_value() && stretched->is_valid());
    assert(!constant_time_equals(stretched->enc_key.span(), stretched->mac_key.span()));

    // Argon2id derivation (Bitwarden KDF type 1, fast test params: 1 MiB, 2 iter, 1 lane)
    BitwardenKdfConfig argon_cfg { BitwardenKdfType::Argon2id, 2, 1, 1 };
    auto amk1 = BitwardenCrypto::derive_master_key("s3cr3t-pass", "Alice@Example.com", argon_cfg);
    auto amk2 = BitwardenCrypto::derive_master_key("s3cr3t-pass", "alice@example.com", argon_cfg);
    auto amk3 = BitwardenCrypto::derive_master_key("different", "alice@example.com", argon_cfg);
    assert(amk1.has_value() && amk2.has_value() && amk3.has_value());
    assert(amk1->size() == 32);
    assert(constant_time_equals(amk1->span(), amk2->span()));
    assert(!constant_time_equals(amk1->span(), amk3->span()));

    // RFC 9106 Argon2id reference check (t=2, m=32 KiB, p=4, pass="password", salt="somesalt", L=32)
    auto rfc_key = derive_argon2id(
        { reinterpret_cast<uint8_t const*>("password"), 8 },
        { reinterpret_cast<uint8_t const*>("somesalt"), 8 },
        2, 32, 4, 32);
    assert(rfc_key.has_value() && rfc_key->size() == 32);

    // AES-256-CBC + HMAC-SHA256 (`2.<iv>|<ct>|<mac>`) round-trip & deterministic IV check
    auto sym_key = BitwardenCrypto::generate_symmetric_key();
    assert(sym_key.has_value() && sym_key->is_valid());

    std::array<uint8_t, 16> fixed_iv { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    std::string_view secret_text = "correct-horse-battery-staple-42!";
    auto cs1 = BitwardenCrypto::encrypt_bytes(
        { reinterpret_cast<uint8_t const*>(secret_text.data()), secret_text.size() },
        *sym_key,
        fixed_iv);
    auto cs2 = BitwardenCrypto::encrypt_bytes(
        { reinterpret_cast<uint8_t const*>(secret_text.data()), secret_text.size() },
        *sym_key,
        fixed_iv);
    assert(cs1.has_value() && cs2.has_value());
    assert(*cs1 == *cs2);
    assert(cs1->starts_with("2."));

    auto decrypted = BitwardenCrypto::decrypt_string(*cs1, *sym_key);
    assert(decrypted.has_value() && *decrypted == secret_text);

    // Tampered MAC MUST be rejected before decryption
    auto parsed = BitwardenCrypto::parse_cipher_string(*cs1);
    assert(parsed.has_value());
    {
        auto bad_mac = parsed->mac;
        bad_mac[0] ^= 0x01;
        std::string tampered = "2." + base64_encode(parsed->iv) + "|" + base64_encode(parsed->ciphertext) + "|" + base64_encode(bad_mac);
        assert(!BitwardenCrypto::decrypt_string(tampered, *sym_key).has_value());
    }
    // Tampered ciphertext MUST be rejected
    {
        auto bad_ct = parsed->ciphertext;
        bad_ct[0] ^= 0x80;
        std::string tampered = "2." + base64_encode(parsed->iv) + "|" + base64_encode(bad_ct) + "|" + base64_encode(parsed->mac);
        assert(!BitwardenCrypto::decrypt_string(tampered, *sym_key).has_value());
    }
    // Tampered IV MUST be rejected
    {
        auto bad_iv = parsed->iv;
        bad_iv[15] ^= 0x01;
        std::string tampered = "2." + base64_encode(bad_iv) + "|" + base64_encode(parsed->ciphertext) + "|" + base64_encode(parsed->mac);
        assert(!BitwardenCrypto::decrypt_string(tampered, *sym_key).has_value());
    }
    // Wrong key MUST be rejected
    auto other_key = BitwardenCrypto::generate_symmetric_key();
    assert(other_key.has_value());
    assert(!BitwardenCrypto::decrypt_string(*cs1, *other_key).has_value());

    // Protected user symmetric key wrap/unwrap with stretched master key
    auto wrapped_user_key = BitwardenCrypto::encrypt_bytes(sym_key->to_bytes().span(), *stretched);
    assert(wrapped_user_key.has_value());
    auto unwrapped_bytes = BitwardenCrypto::decrypt_bytes(*wrapped_user_key, *stretched);
    assert(unwrapped_bytes.has_value() && unwrapped_bytes->size() == 64);
    auto unwrapped_key = BitwardenSymmetricKey::from_bytes(unwrapped_bytes->span());
    assert(unwrapped_key.has_value());
    assert(constant_time_equals(unwrapped_key->enc_key.span(), sym_key->enc_key.span()));
    assert(constant_time_equals(unwrapped_key->mac_key.span(), sym_key->mac_key.span()));

    // RSA-OAEP Type 4 (SHA-1) & Type 3 (SHA-256) organization key wrap/unwrap
    auto rsa_pair = generate_test_rsa_keypair();
    auto org_key = BitwardenCrypto::generate_symmetric_key();
    assert(org_key.has_value());

    auto enc_org_sha1 = BitwardenCrypto::rsa_encrypt(org_key->to_bytes().span(), rsa_pair.spki_public_der, BitwardenCipherType::Rsa2048_OaepSha1_B64);
    assert(enc_org_sha1.has_value() && enc_org_sha1->starts_with("4."));
    auto dec_org_sha1 = BitwardenCrypto::rsa_decrypt(*enc_org_sha1, rsa_pair.pkcs8_private_der);
    assert(dec_org_sha1.has_value() && constant_time_equals(dec_org_sha1->span(), org_key->to_bytes().span()));

    auto enc_org_sha256 = BitwardenCrypto::rsa_encrypt(org_key->to_bytes().span(), rsa_pair.spki_public_der, BitwardenCipherType::Rsa2048_OaepSha256_B64);
    assert(enc_org_sha256.has_value() && enc_org_sha256->starts_with("3."));
    auto dec_org_sha256 = BitwardenCrypto::rsa_decrypt(*enc_org_sha256, rsa_pair.pkcs8_private_der);
    assert(dec_org_sha256.has_value() && constant_time_equals(dec_org_sha256->span(), org_key->to_bytes().span()));

    std::cout << "  [PASS] BitwardenCrypto (PBKDF2, Argon2id, HKDF, AES-CBC+HMAC, tampered MAC, RSA-OAEP)\n";
}

void test_local_vault_crypto()
{
    auto kdf = LocalVaultCrypto::create_default_kdf_params(2, 1024, 1);
    assert(kdf.has_value());
    auto kek = LocalVaultCrypto::derive_kek("my-local-master-passphrase", *kdf);
    assert(kek.has_value() && kek->size() == 32);

    auto dek = LocalVaultCrypto::generate_dek();
    assert(dek.has_value() && dek->size() == 32);

    auto wrapped_dek = LocalVaultCrypto::wrap_dek(kek->span(), dek->span());
    assert(wrapped_dek.has_value());
    auto unwrapped_dek = LocalVaultCrypto::unwrap_dek(kek->span(), *wrapped_dek);
    assert(unwrapped_dek.has_value() && constant_time_equals(unwrapped_dek->span(), dek->span()));

    // Wrong passphrase KEK fails to unwrap DEK
    auto bad_kek = LocalVaultCrypto::derive_kek("wrong-passphrase", *kdf);
    assert(bad_kek.has_value());
    assert(!LocalVaultCrypto::unwrap_dek(bad_kek->span(), *wrapped_dek).has_value());

    // Item AES-256-GCM round-trip with item-id AAD
    std::string json = R"({"name":"GitHub","username":"octocat","password":"p@ss!"})";
    auto sealed = LocalVaultCrypto::encrypt_item(dek->span(), "item-001", json);
    assert(sealed.has_value());
    auto opened = LocalVaultCrypto::decrypt_item(dek->span(), "item-001", *sealed);
    assert(opened.has_value() && *opened == json);

    // Wrong AAD (item ID swap attack) MUST fail
    assert(!LocalVaultCrypto::decrypt_item(dek->span(), "item-002", *sealed).has_value());

    // Tampered tag MUST fail
    auto tampered = *sealed;
    tampered.tag[0] ^= 0xFF;
    assert(!LocalVaultCrypto::decrypt_item(dek->span(), "item-001", tampered).has_value());

    std::cout << "  [PASS] LocalVaultCrypto (Argon2id KEK, DEK wrap, AES-256-GCM item AAD)\n";
}

void test_totp_rfc6238()
{
    // RFC 6238 Appendix B test vectors (8-digit TOTP, period 30s)
    // SHA-1 secret: "12345678901234567890" (20 bytes)
    // SHA-256 secret: "12345678901234567890123456789012" (32 bytes)
    // SHA-512 secret: "1234567890123456789012345678901234567890123456789012345678901234" (64 bytes)
    std::string s20 = "12345678901234567890";
    std::string s32 = "12345678901234567890123456789012";
    std::string s64 = "1234567890123456789012345678901234567890123456789012345678901234";

    TotpConfig sha1_cfg { { s20.begin(), s20.end() }, TotpAlgorithm::Sha1, 8, 30, "", "" };
    TotpConfig sha256_cfg { { s32.begin(), s32.end() }, TotpAlgorithm::Sha256, 8, 30, "", "" };
    TotpConfig sha512_cfg { { s64.begin(), s64.end() }, TotpAlgorithm::Sha512, 8, 30, "", "" };

    struct Vec {
        int64_t t;
        char const* sha1;
        char const* sha256;
        char const* sha512;
    };
    Vec vectors[] = {
        { 59, "94287082", "46119246", "90693936" },
        { 1111111109, "07081804", "68084774", "25091201" },
        { 1111111111, "14050471", "67062674", "99943326" },
        { 1234567890, "89005924", "91819424", "93441116" },
        { 2000000000, "69279037", "90698825", "38618901" },
    };

    for (auto const& v : vectors) {
        auto r1 = Totp::generate(sha1_cfg, v.t);
        auto r2 = Totp::generate(sha256_cfg, v.t);
        auto r3 = Totp::generate(sha512_cfg, v.t);
        assert(r1 && r1->code == v.sha1);
        assert(r2 && r2->code == v.sha256);
        assert(r3 && r3->code == v.sha512);
    }

    // otpauth:// URI parsing (Base32 of "12345678901234567890" is GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ)
    auto uri_res = Totp::generate_from_uri(
        "otpauth://totp/Example:alice%40example.com?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=Example&algorithm=SHA1&digits=8&period=30",
        59);
    assert(uri_res.has_value() && uri_res->code == "94287082" && uri_res->seconds_remaining == 1);

    // Raw Base32 secret with spaces/lowercase/hyphens (default 6 digits -> last 6 digits of 94287082 = "287082")
    auto raw_res = Totp::generate_from_uri("gezd-gnbv gy3t qojq gezd gnbv gy3t qojq", 59);
    assert(raw_res.has_value() && raw_res->code == "287082");

    std::cout << "  [PASS] Totp (RFC 6238 SHA-1/SHA-256/SHA-512 vectors + otpauth:// + Base32)\n";
}

void test_uri_matcher()
{
    assert(UriMatcher::registrable_domain("login.github.com") == "github.com");
    assert(UriMatcher::registrable_domain("sub.service.co.uk") == "service.co.uk");
    assert(UriMatcher::registrable_domain("myorg.github.io") == "myorg.github.io");
    assert(UriMatcher::registrable_domain("127.0.0.1") == "127.0.0.1");
    assert(UriMatcher::registrable_domain("localhost") == "localhost");

    // BaseDomain matches across subdomains, does not match different eTLD+1
    assert(UriMatcher::match_rule("https://app.example.co.uk/signin", { "https://auth.example.co.uk", UriMatchType::BaseDomain }).matched);
    assert(!UriMatcher::match_rule("https://evil.co.uk/signin", { "https://auth.example.co.uk", UriMatchType::BaseDomain }).matched);

    // Host requires exact host (and port if specified in rule)
    assert(UriMatcher::match_rule("https://app.example.com/a", { "https://app.example.com/b", UriMatchType::Host }).matched);
    assert(!UriMatcher::match_rule("https://other.example.com/a", { "https://app.example.com/b", UriMatchType::Host }).matched);
    assert(!UriMatcher::match_rule("http://localhost:8080/", { "http://localhost:9090/", UriMatchType::Host }).matched);
    assert(UriMatcher::match_rule("http://localhost:8080/login", { "http://localhost:8080/", UriMatchType::Host }).matched);

    // StartsWith
    assert(UriMatcher::match_rule("https://example.com/admin/login", { "https://example.com/admin", UriMatchType::StartsWith }).matched);
    assert(!UriMatcher::match_rule("https://example.com/user/login", { "https://example.com/admin", UriMatchType::StartsWith }).matched);

    // Exact
    assert(UriMatcher::match_rule("https://example.com/login", { "https://example.com/login", UriMatchType::Exact }).matched);
    assert(!UriMatcher::match_rule("https://example.com/login?next=/", { "https://example.com/login", UriMatchType::Exact }).matched);

    // Regex (and invalid regex doesn't throw)
    assert(UriMatcher::match_rule("https://s3.example.com/auth", { R"(^https://s[0-9]+\.example\.com/auth$)", UriMatchType::RegularExpression }).matched);
    assert(!UriMatcher::match_rule("https://example.com/", { "[unclosed-regex(", UriMatchType::RegularExpression }).matched);

    // Never
    assert(!UriMatcher::match_rule("https://example.com/login", { "https://example.com/login", UriMatchType::Never }).matched);

    // Ranking: Exact > StartsWith > Host > BaseDomain; favorite boosts within tier
    std::vector<CredentialSummary> creds = {
        { ProviderKind::LocalVault, "local", "base-plain", "Base Plain", "u1", { { "https://other.example.com", UriMatchType::BaseDomain } }, true, false, false },
        { ProviderKind::LocalVault, "local", "base-fav", "Base Fav", "u2", { { "https://other.example.com", UriMatchType::BaseDomain } }, true, false, true },
        { ProviderKind::LocalVault, "local", "host", "Host Match", "u3", { { "https://app.example.com", UriMatchType::Host } }, true, false, false },
        { ProviderKind::LocalVault, "local", "prefix", "Prefix Match", "u4", { { "https://app.example.com/auth", UriMatchType::StartsWith } }, true, false, false },
        { ProviderKind::LocalVault, "local", "exact", "Exact Match", "u5", { { "https://app.example.com/auth/login", UriMatchType::Exact } }, true, false, false },
        { ProviderKind::LocalVault, "local", "never", "Never Match", "u6", { { "https://app.example.com/auth/login", UriMatchType::Never } }, true, false, true },
    };

    auto ranked = UriMatcher::filter_and_rank("https://app.example.com/auth/login", creds);
    assert(ranked.size() == 5);
    assert(ranked[0].id == "exact");
    assert(ranked[1].id == "prefix");
    assert(ranked[2].id == "host");
    assert(ranked[3].id == "base-fav");
    assert(ranked[4].id == "base-plain");

    // Search
    auto found = UriMatcher::search("u4", creds);
    assert(found.size() == 1 && found[0].id == "prefix");

    std::cout << "  [PASS] UriMatcher (BaseDomain/Host/StartsWith/Exact/Regex/Never + ranking)\n";
}

void test_password_generator()
{
    PasswordGeneratorOptions opts;
    opts.length = 24;
    opts.uppercase = true;
    opts.lowercase = true;
    opts.digits = true;
    opts.symbols = true;
    opts.avoid_ambiguous = true;

    std::string p1 = PasswordGenerator::generate_password(opts);
    std::string p2 = PasswordGenerator::generate_password(opts);
    assert(p1.size() == 24);
    assert(p2.size() == 24);
    assert(p1 != p2);

    bool has_u = false, has_l = false, has_d = false, has_s = false;
    for (char c : p1) {
        assert(c != 'I' && c != 'l' && c != '1' && c != 'O' && c != '0');
        if (c >= 'A' && c <= 'Z')
            has_u = true;
        else if (c >= 'a' && c <= 'z')
            has_l = true;
        else if (c >= '0' && c <= '9')
            has_d = true;
        else
            has_s = true;
    }
    assert(has_u && has_l && has_d && has_s);

    auto strength = PasswordGenerator::estimate_strength(p1);
    assert(strength.entropy_bits > 95.0 && strength.label == "Very Strong");
    assert(PasswordGenerator::estimate_strength("12345").label == "Weak");

    PassphraseGeneratorOptions pp_opts { 5, '-', true, true };
    std::string phrase = PasswordGenerator::generate_passphrase(pp_opts);
    size_t dashes = 0;
    bool has_digit = false;
    for (char c : phrase) {
        if (c == '-')
            ++dashes;
        if (c >= '0' && c <= '9')
            has_digit = true;
    }
    assert(dashes == 4 && has_digit);

    std::cout << "  [PASS] PasswordGenerator (CSPRNG, classes, avoid_ambiguous, passphrase, strength)\n";
}

void test_csv_transfer()
{
    // Apple Passwords / Safari CSV with UTF-8 BOM, quotes, commas, newlines, OTPAuth
    std::string apple_csv = "\xEF\xBB\xBF"
                            "Title,URL,Username,Password,Notes,OTPAuth\n"
                            "\"GitHub, Inc.\",https://github.com/login,octocat,\"p@ss\"\"word,1\",\"line1\nline2\",otpauth://totp/GitHub:octocat?secret=JBSWY3DPEHPK3PXP\n"
                            "Example,https://example.com,alice,secret123,,\n";
    auto res_apple = CsvTransfer::import_csv(apple_csv);
    assert(res_apple.detected_format == CsvFormat::ApplePasswords);
    assert(res_apple.logins.size() == 2);
    assert(res_apple.logins[0].name == "GitHub, Inc.");
    assert(res_apple.logins[0].password == "p@ss\"word,1");
    assert(res_apple.logins[0].notes.value_or("") == "line1\nline2");
    assert(res_apple.logins[0].totp_uri.value_or("") == "otpauth://totp/GitHub:octocat?secret=JBSWY3DPEHPK3PXP");

    // Export and re-import round-trip
    auto exported = CsvTransfer::export_csv(res_apple.logins, CsvFormat::ApplePasswords);
    auto reimported = CsvTransfer::import_csv(exported);
    assert(reimported.detected_format == CsvFormat::ApplePasswords);
    assert(reimported.logins.size() == 2);
    assert(reimported.logins[0].password == "p@ss\"word,1");
    assert(reimported.logins[0].notes == res_apple.logins[0].notes);

    // Chrome CSV
    std::string chrome_csv = "name,url,username,password,note\nNews,https://news.ycombinator.com/login,pg,hunter2,hi\n";
    auto res_chrome = CsvTransfer::import_csv(chrome_csv);
    assert(res_chrome.detected_format == CsvFormat::Chrome);
    assert(res_chrome.logins.size() == 1 && res_chrome.logins[0].username == "pg");

    // Bitwarden CSV (skips note type, parses multiple URIs)
    std::string bw_csv = "folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n"
                         ",1,login,Work Mail,my note,,0,\"https://mail.work.com,https://sso.work.com\",bob,pw123,JBSWY3DPEHPK3PXP\n"
                         ",,note,Secure Note,not a login,,0,,,,\n";
    auto res_bw = CsvTransfer::import_csv(bw_csv);
    assert(res_bw.detected_format == CsvFormat::Bitwarden);
    assert(res_bw.logins.size() == 1);
    assert(res_bw.skipped_rows == 1);
    assert(res_bw.logins[0].favorite);
    assert(res_bw.logins[0].uris.size() == 2);
    assert(res_bw.logins[0].uris[1].uri == "https://sso.work.com");

    // 1Password CSV
    std::string op_csv = "Title,Url,Username,Password,OTPAuth,Notes\nStripe,https://dashboard.stripe.com,dev@co.io,str1pe!,otpauth://totp/Stripe?secret=JBSWY3DPEHPK3PXP,billing\n";
    auto res_op = CsvTransfer::import_csv(op_csv);
    assert(res_op.detected_format == CsvFormat::OnePassword);
    assert(res_op.logins.size() == 1 && res_op.logins[0].name == "Stripe");

    std::cout << "  [PASS] CsvTransfer (Apple Passwords, Chrome, Bitwarden, 1Password + round-trip)\n";
}

void test_onepassword_parser()
{
    std::string list_json = R"([
      {
        "id": "abc123",
        "title": "GitHub",
        "additional_information": "octocat",
        "favorite": true,
        "urls": [
          {"primary": true, "href": "https://github.com/login"},
          {"href": "https://gist.github.com"}
        ]
      },
      {
        "id": "def456",
        "title": "Linear",
        "additional_information": "dev@superbird.dev",
        "tags": ["Favorite", "Work"],
        "urls": [{"href": "https://linear.app"}]
      }
    ])";
    auto list = OnePasswordParser::parse_item_list(list_json, "personal");
    assert(list.has_value() && list->size() == 2);
    assert((*list)[0].id == "abc123" && (*list)[0].username == "octocat" && (*list)[0].favorite && (*list)[0].uris.size() == 2);
    assert((*list)[1].id == "def456" && (*list)[1].favorite);

    std::string detail_json = R"({
      "id": "abc123",
      "title": "GitHub",
      "fields": [
        {"id": "username", "type": "STRING", "purpose": "USERNAME", "value": "octocat"},
        {"id": "password", "type": "CONCEALED", "purpose": "PASSWORD", "value": "s3cr3t-gh-pw"},
        {"id": "otp1", "type": "OTP", "totp": "654321", "value": "otpauth://totp/GitHub:octocat?secret=JBSWY3DPEHPK3PXP"},
        {"id": "notesPlain", "type": "STRING", "purpose": "NOTES", "value": "recovery codes stored"}
      ]
    })";
    auto detail = OnePasswordParser::parse_item_detail(detail_json);
    assert(detail.has_value());
    assert(detail->username == "octocat");
    assert(detail->password == "s3cr3t-gh-pw");
    assert(detail->precomputed_totp.value_or("") == "654321");
    assert(detail->totp_uri.has_value());
    assert(detail->notes.value_or("") == "recovery codes stored");

    assert(OnePasswordParser::parse_otp_output(" 482910 \n").value_or("") == "482910");
    assert(!OnePasswordParser::parse_otp_output("ERROR: no otp").has_value());

    std::cout << "  [PASS] OnePasswordParser (`op` item list, item reveal, OTP parsing)\n";
}

void test_passwords_actions()
{
    class MockHandler final : public PasswordsCommandHandler {
    public:
        bool enabled { true };
        std::vector<PasswordsCommand> invoked;

        virtual bool can_run(PasswordsCommand) const override { return enabled; }
        virtual void run(PasswordsCommand cmd) override { invoked.push_back(cmd); }
    };

    ModuleRegistry registry;
    registry.register_module({
        .id = PASSWORDS_MODULE_ID,
        .name = "Passwords"sv,
        .description = "Multi-provider password manager"sv,
        .enabled_by_default = true,
    });
    MockHandler handler;
    register_passwords_actions(registry, handler);

    assert(registry.matching_actions().size() == PASSWORDS_ACTIONS.size());
    auto const* autofill = registry.action("passwords.autofill"sv);
    assert(autofill != nullptr);
    assert(autofill->shortcut.has_value());
    assert(autofill->available());
    autofill->trigger();
    assert(handler.invoked.size() == 1 && handler.invoked[0] == PasswordsCommand::AutofillLogin);

    registry.set_enabled(PASSWORDS_MODULE_ID, false);
    assert(registry.matching_actions().is_empty());
    registry.set_enabled(PASSWORDS_MODULE_ID, true);
    assert(registry.matching_actions().size() == PASSWORDS_ACTIONS.size());

    std::cout << "  [PASS] PasswordsActions (ModuleRegistry bus, shortcuts, enable gating)\n";
}

// Minimal HTTP/1.1 client over loopback TCP for the mock Bitwarden/Vaultwarden server integration test.
struct HttpReply {
    int status { 0 };
    std::string body;
};

HttpReply send_loopback_http(uint16_t port, BitwardenHttpRequest const& req)
{
    auto path_pos = req.url.find("://");
    assert(path_pos != std::string::npos);
    auto slash = req.url.find('/', path_pos + 3);
    std::string path = (slash == std::string::npos) ? "/" : req.url.substr(slash);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    std::ostringstream wire;
    wire << req.method << " " << path << " HTTP/1.1\r\n";
    wire << "Host: 127.0.0.1:" << port << "\r\n";
    wire << "Connection: close\r\n";
    for (auto const& [k, v] : req.headers)
        wire << k << ": " << v << "\r\n";
    wire << "Content-Length: " << req.body.size() << "\r\n\r\n";
    wire << req.body;

    std::string payload = wire.str();
    size_t sent = 0;
    while (sent < payload.size()) {
        ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, 0);
        assert(n > 0);
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        raw.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    HttpReply reply;
    auto sp1 = raw.find(' ');
    assert(sp1 != std::string::npos);
    reply.status = std::stoi(raw.substr(sp1 + 1, 3));
    auto sep = raw.find("\r\n\r\n");
    assert(sep != std::string::npos);
    reply.body = raw.substr(sep + 4);
    return reply;
}

void test_mock_bitwarden_server_e2e()
{
    // Prepare realistic encrypted server state for alice@vault.local
    std::string const email = "alice@vault.local";
    std::string const master_password = "SuperSecretMasterPassword!99";
    BitwardenKdfConfig const kdf { BitwardenKdfType::Argon2id, 2, 1, 1 };

    auto mk = BitwardenCrypto::derive_master_key(master_password, email, kdf);
    assert(mk.has_value());
    auto expected_mp_hash = BitwardenCrypto::derive_master_password_hash(mk->span(), master_password);
    assert(expected_mp_hash.has_value());
    auto stretched = BitwardenCrypto::stretch_master_key(mk->span());
    assert(stretched.has_value());

    auto user_key = BitwardenCrypto::generate_symmetric_key();
    assert(user_key.has_value());
    auto protected_user_key = BitwardenCrypto::encrypt_bytes(user_key->to_bytes().span(), *stretched);
    assert(protected_user_key.has_value());

    auto rsa_pair = generate_test_rsa_keypair();
    auto enc_private_key = BitwardenCrypto::encrypt_bytes(rsa_pair.pkcs8_private_der, *user_key);
    assert(enc_private_key.has_value());

    auto org_key = BitwardenCrypto::generate_symmetric_key();
    assert(org_key.has_value());
    auto enc_org_key = BitwardenCrypto::rsa_encrypt(org_key->to_bytes().span(), rsa_pair.spki_public_der, BitwardenCipherType::Rsa2048_OaepSha1_B64);
    assert(enc_org_key.has_value());

    // Personal cipher with per-cipher key
    auto per_cipher_key = BitwardenCrypto::generate_symmetric_key();
    assert(per_cipher_key.has_value());
    auto enc_cipher_key = BitwardenCrypto::encrypt_bytes(per_cipher_key->to_bytes().span(), *user_key);
    auto enc_name_1 = BitwardenCrypto::encrypt_string("GitHub Personal", *per_cipher_key);
    auto enc_user_1 = BitwardenCrypto::encrypt_string("alice-gh", *per_cipher_key);
    auto enc_pass_1 = BitwardenCrypto::encrypt_string("gh-p@ss-123", *per_cipher_key);
    auto enc_totp_1 = BitwardenCrypto::encrypt_string("JBSWY3DPEHPK3PXP", *per_cipher_key);
    auto enc_uri_1 = BitwardenCrypto::encrypt_string("https://github.com/login", *per_cipher_key);

    // Organization cipher encrypted with org_key
    auto enc_name_2 = BitwardenCrypto::encrypt_string("AWS Production", *org_key);
    auto enc_user_2 = BitwardenCrypto::encrypt_string("ops@acme.io", *org_key);
    auto enc_pass_2 = BitwardenCrypto::encrypt_string("aws-org-secret", *org_key);
    auto enc_uri_2 = BitwardenCrypto::encrypt_string("https://console.aws.amazon.com", *org_key);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in bind_addr {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind_addr.sin_port = 0;
    assert(bind(listen_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == 0);
    socklen_t len = sizeof(bind_addr);
    assert(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bind_addr), &len) == 0);
    uint16_t port = ntohs(bind_addr.sin_port);
    assert(listen(listen_fd, 16) == 0);

    std::atomic<bool> stop { false };
    std::string stored_created_body;
    std::string stored_updated_body;

    std::thread server([&] {
        while (!stop.load()) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listen_fd, &rfds);
            timeval tv { 0, 50000 };
            int ready = select(listen_fd + 1, &rfds, nullptr, nullptr, &tv);
            if (ready <= 0)
                continue;
            int client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd < 0)
                continue;

            std::string req_raw;
            char buf[4096];
            size_t header_end = std::string::npos;
            size_t content_length = 0;
            while (true) {
                ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                req_raw.append(buf, static_cast<size_t>(n));
                if (header_end == std::string::npos) {
                    header_end = req_raw.find("\r\n\r\n");
                    if (header_end != std::string::npos) {
                        auto cl_pos = req_raw.find("Content-Length:");
                        if (cl_pos != std::string::npos && cl_pos < header_end) {
                            auto nl = req_raw.find("\r\n", cl_pos);
                            content_length = static_cast<size_t>(std::stoul(req_raw.substr(cl_pos + 15, nl - (cl_pos + 15))));
                        }
                    }
                }
                if (header_end != std::string::npos && req_raw.size() >= header_end + 4 + content_length)
                    break;
            }

            auto line_end = req_raw.find("\r\n");
            auto req_line = req_raw.substr(0, line_end);
            auto sp1 = req_line.find(' ');
            auto sp2 = req_line.find(' ', sp1 + 1);
            std::string method = req_line.substr(0, sp1);
            std::string path = req_line.substr(sp1 + 1, sp2 - sp1 - 1);
            std::string body = (header_end != std::string::npos) ? req_raw.substr(header_end + 4) : "";

            int status = 404;
            std::string resp_body = R"({"error":"not_found"})";

            if (method == "POST" && path == "/identity/accounts/prelogin") {
                status = 200;
                resp_body = R"({"kdf":1,"kdfIterations":2,"kdfMemory":1,"kdfParallelism":1})";
            } else if (method == "POST" && path == "/identity/connect/token") {
                if (body.find("grant_type=client_credentials") != std::string::npos) {
                    if (body.find("client_id=user.test") != std::string::npos && body.find("client_secret=sec123") != std::string::npos) {
                        status = 200;
                        resp_body = std::string(R"({"access_token":"tok-apikey","expires_in":3600,"Key":")")
                            + BitwardenProtocol::json_escape(*protected_user_key)
                            + R"(","Kdf":1,"KdfIterations":2,"KdfMemory":1,"KdfParallelism":1})";
                    } else {
                        status = 400;
                        resp_body = R"({"error":"invalid_client","error_description":"Invalid API key"})";
                    }
                } else if (body.find("grant_type=refresh_token") != std::string::npos) {
                    if (body.find("refresh_token=ref-001") != std::string::npos) {
                        status = 200;
                        resp_body = R"({"access_token":"tok-refreshed","refresh_token":"ref-002","expires_in":3600})";
                    } else {
                        status = 400;
                        resp_body = R"({"error":"invalid_grant"})";
                    }
                } else if (body.find("grant_type=password") != std::string::npos) {
                    if (body.find("twoFactorToken=123456") != std::string::npos && body.find("newDeviceOtp=998877") != std::string::npos) {
                        status = 200;
                        resp_body = std::string(R"({"access_token":"tok-full","refresh_token":"ref-001","expires_in":3600,"Key":")")
                            + BitwardenProtocol::json_escape(*protected_user_key)
                            + R"(","PrivateKey":")" + BitwardenProtocol::json_escape(*enc_private_key)
                            + R"(","Kdf":1,"KdfIterations":2,"KdfMemory":1,"KdfParallelism":1})";
                    } else if (body.find("twoFactorToken=123456") != std::string::npos) {
                        status = 400;
                        resp_body = R"({"error":"device_error","error_description":"New device verification required"})";
                    } else {
                        status = 400;
                        resp_body = R"({"error":"invalid_grant","error_description":"Two factor required.","TwoFactorProviders2":{"0":null,"1":{"Email":"a***@vault.local"}}})";
                    }
                }
            } else if (method == "GET" && path.starts_with("/api/sync")) {
                if (req_raw.find("Authorization: Bearer tok-full") != std::string::npos) {
                    status = 200;
                    resp_body = std::string(R"({"profile":{"email":"alice@vault.local","key":")")
                        + BitwardenProtocol::json_escape(*protected_user_key)
                        + R"(","privateKey":")" + BitwardenProtocol::json_escape(*enc_private_key)
                        + R"(","organizations":[{"id":"org-1","key":")" + BitwardenProtocol::json_escape(*enc_org_key)
                        + R"("}]},"ciphers":[)"
                        + R"({"id":"c-1","type":1,"organizationId":null,"favorite":true,"key":")" + BitwardenProtocol::json_escape(*enc_cipher_key)
                        + R"(","name":")" + BitwardenProtocol::json_escape(*enc_name_1)
                        + R"(","login":{"username":")" + BitwardenProtocol::json_escape(*enc_user_1)
                        + R"(","password":")" + BitwardenProtocol::json_escape(*enc_pass_1)
                        + R"(","totp":")" + BitwardenProtocol::json_escape(*enc_totp_1)
                        + R"(","uris":[{"uri":")" + BitwardenProtocol::json_escape(*enc_uri_1) + R"(","match":0}]}},)"
                        + R"({"id":"c-2","type":1,"organizationId":"org-1","favorite":false,"name":")" + BitwardenProtocol::json_escape(*enc_name_2)
                        + R"(","login":{"username":")" + BitwardenProtocol::json_escape(*enc_user_2)
                        + R"(","password":")" + BitwardenProtocol::json_escape(*enc_pass_2)
                        + R"(","uris":[{"uri":")" + BitwardenProtocol::json_escape(*enc_uri_2) + R"(","match":1}]}}]})";
                } else {
                    status = 401;
                    resp_body = R"({"error":"unauthorized"})";
                }
            } else if (method == "POST" && path == "/api/ciphers") {
                stored_created_body = body;
                // Inject `"id":"c-new-1",` at start of object
                status = 200;
                resp_body = "{\"id\":\"c-new-1\"," + body.substr(1);
            } else if (method == "PUT" && path == "/api/ciphers/c-new-1") {
                stored_updated_body = body;
                status = 200;
                resp_body = "{\"id\":\"c-new-1\"," + body.substr(1);
            }

            std::ostringstream out;
            out << "HTTP/1.1 " << status << " OK\r\n"
                << "Content-Type: application/json\r\n"
                << "Content-Length: " << resp_body.size() << "\r\n"
                << "Connection: close\r\n\r\n"
                << resp_body;
            std::string s = out.str();
            send(client_fd, s.data(), s.size(), 0);
            close(client_fd);
        }
    });

    auto endpoints = BitwardenEndpoints::for_region(BitwardenRegion::Custom, "http://127.0.0.1:" + std::to_string(port));

    // 1. Prelogin
    auto pre_reply = send_loopback_http(port, BitwardenProtocol::build_prelogin_request(endpoints, email));
    assert(pre_reply.status == 200);
    auto parsed_kdf = BitwardenProtocol::parse_prelogin_response(pre_reply.body);
    assert(parsed_kdf.has_value() && parsed_kdf->type == BitwardenKdfType::Argon2id && parsed_kdf->iterations == 2);

    // 2. Password login -> 2FA challenge
    auto tok1_reply = send_loopback_http(port, BitwardenProtocol::build_password_token_request(endpoints, email, *expected_mp_hash, "dev-1", "Superbird"));
    auto tok1 = BitwardenProtocol::parse_token_response(tok1_reply.status, tok1_reply.body);
    assert(!tok1.ok && tok1.challenge.has_value() && !tok1.challenge->is_new_device_verification);
    assert(tok1.challenge->available_providers.size() == 2 && tok1.challenge->available_providers[0] == 0);

    // 3. Retry with 2FA -> New Device Verification challenge
    auto tok2_reply = send_loopback_http(port, BitwardenProtocol::build_password_token_request(endpoints, email, *expected_mp_hash, "dev-1", "Superbird", 0, "123456"));
    auto tok2 = BitwardenProtocol::parse_token_response(tok2_reply.status, tok2_reply.body);
    assert(!tok2.ok && tok2.challenge.has_value() && tok2.challenge->is_new_device_verification);

    // 4. Retry with 2FA + newDeviceOtp -> full token + protected symmetric key
    auto tok3_reply = send_loopback_http(port, BitwardenProtocol::build_password_token_request(endpoints, email, *expected_mp_hash, "dev-1", "Superbird", 0, "123456", "998877"));
    auto tok3 = BitwardenProtocol::parse_token_response(tok3_reply.status, tok3_reply.body);
    assert(tok3.ok && tok3.access_token == "tok-full" && tok3.refresh_token == "ref-001");

    // 5. API Key login path
    auto api_reply = send_loopback_http(port, BitwardenProtocol::build_api_key_token_request(endpoints, "user.test", "sec123", "dev-1", "Superbird"));
    auto api_tok = BitwardenProtocol::parse_token_response(api_reply.status, api_reply.body);
    assert(api_tok.ok && api_tok.access_token == "tok-apikey");

    // 6. Refresh token path
    auto ref_reply = send_loopback_http(port, BitwardenProtocol::build_refresh_token_request(endpoints, tok3.refresh_token));
    auto ref_tok = BitwardenProtocol::parse_token_response(ref_reply.status, ref_reply.body);
    assert(ref_tok.ok && ref_tok.access_token == "tok-refreshed" && ref_tok.refresh_token == "ref-002");

    // 7. Sync + decrypt user key + RSA-OAEP org key + per-cipher key + org cipher
    auto sync_reply = send_loopback_http(port, BitwardenProtocol::build_sync_request(endpoints, tok3.access_token));
    auto sync = BitwardenProtocol::parse_sync_response(sync_reply.status, sync_reply.body);
    assert(sync.ok && sync.ciphers.size() == 2);

    auto unwrapped_uk_bytes = BitwardenCrypto::decrypt_bytes(sync.protected_symmetric_key, *stretched);
    assert(unwrapped_uk_bytes.has_value());
    auto unwrapped_uk = BitwardenSymmetricKey::from_bytes(unwrapped_uk_bytes->span());
    assert(unwrapped_uk.has_value());

    auto org_keys = BitwardenProtocol::unwrap_organization_keys(sync.encrypted_private_key, sync.encrypted_org_keys, *unwrapped_uk);
    assert(org_keys.size() == 1 && org_keys.contains("org-1"));

    auto dec1 = BitwardenProtocol::decrypt_cipher(sync.ciphers[0], *unwrapped_uk, org_keys, email);
    assert(dec1.has_value());
    assert(dec1->summary.name == "GitHub Personal");
    assert(dec1->secret.username == "alice-gh" && dec1->secret.password == "gh-p@ss-123");
    assert(dec1->summary.has_totp && dec1->secret.totp_uri.value_or("") == "JBSWY3DPEHPK3PXP");

    auto dec2 = BitwardenProtocol::decrypt_cipher(sync.ciphers[1], *unwrapped_uk, org_keys, email);
    assert(dec2.has_value());
    assert(dec2->summary.name == "AWS Production");
    assert(dec2->secret.username == "ops@acme.io" && dec2->secret.password == "aws-org-secret");

    // 8. POST /api/ciphers (create) + decrypt server echo
    NewLogin created_login;
    created_login.name = "Superbird Forum";
    created_login.username = "caio";
    created_login.password = "new-created-pw!1";
    created_login.uris.push_back({ "https://forum.superbird.dev", UriMatchType::BaseDomain });
    auto create_req = BitwardenProtocol::build_create_cipher_request(endpoints, tok3.access_token, created_login, *unwrapped_uk);
    assert(create_req.has_value());
    auto create_reply = send_loopback_http(port, *create_req);
    auto created_raw = BitwardenProtocol::parse_single_cipher_response(create_reply.status, create_reply.body);
    assert(created_raw.has_value() && created_raw->id == "c-new-1");
    auto created_dec = BitwardenProtocol::decrypt_cipher(*created_raw, *unwrapped_uk, org_keys, email);
    assert(created_dec.has_value() && created_dec->secret.password == "new-created-pw!1");
    // Ensure plaintext never appeared on the wire!
    assert(stored_created_body.find("new-created-pw!1") == std::string::npos);

    // 9. PUT /api/ciphers/{id} (update) + decrypt server echo
    created_login.password = "rotated-pw!2";
    auto update_req = BitwardenProtocol::build_update_cipher_request(endpoints, tok3.access_token, "c-new-1", created_login, *unwrapped_uk);
    assert(update_req.has_value());
    auto update_reply = send_loopback_http(port, *update_req);
    auto updated_raw = BitwardenProtocol::parse_single_cipher_response(update_reply.status, update_reply.body);
    assert(updated_raw.has_value());
    auto updated_dec = BitwardenProtocol::decrypt_cipher(*updated_raw, *unwrapped_uk, org_keys, email);
    assert(updated_dec.has_value() && updated_dec->secret.password == "rotated-pw!2");
    assert(stored_updated_body.find("rotated-pw!2") == std::string::npos);

    stop.store(true);
    server.join();
    close(listen_fd);

    std::cout << "  [PASS] Mock Bitwarden/Vaultwarden HTTP server E2E (prelogin -> 2FA/newDeviceOtp/APIKey/refresh -> sync -> decrypt -> create/update)\n";
}

} // namespace

int main()
{
    std::cout << "Running TestPasswords...\n";
    test_bitwarden_crypto();
    test_local_vault_crypto();
    test_totp_rfc6238();
    test_uri_matcher();
    test_password_generator();
    test_csv_transfer();
    test_onepassword_parser();
    test_passwords_actions();
    test_mock_bitwarden_server_e2e();
    std::cout << "All TestPasswords checks passed!\n";
    return 0;
}
