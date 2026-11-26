/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

namespace Superbird::Passwords {

void cleanse_memory(void* ptr, size_t len)
{
    if (ptr && len > 0)
        OPENSSL_cleanse(ptr, len);
}

void cleanse_string(std::string& str)
{
    if (!str.empty())
        OPENSSL_cleanse(str.data(), str.size());
    str.clear();
}

SecureBytes::SecureBytes(size_t size)
    : m_data(size, 0)
{
}

SecureBytes::SecureBytes(std::span<uint8_t const> data)
    : m_data(data.begin(), data.end())
{
}

SecureBytes::SecureBytes(std::vector<uint8_t>&& data)
    : m_data(std::move(data))
{
}

SecureBytes::~SecureBytes()
{
    clear();
}

SecureBytes::SecureBytes(SecureBytes const& other)
    : m_data(other.m_data)
{
}

SecureBytes& SecureBytes::operator=(SecureBytes const& other)
{
    if (this != &other) {
        clear();
        m_data = other.m_data;
    }
    return *this;
}

SecureBytes::SecureBytes(SecureBytes&& other) noexcept
    : m_data(std::move(other.m_data))
{
}

SecureBytes& SecureBytes::operator=(SecureBytes&& other) noexcept
{
    if (this != &other) {
        clear();
        m_data = std::move(other.m_data);
    }
    return *this;
}

void SecureBytes::clear()
{
    cleanse_memory(m_data.data(), m_data.size());
    m_data.clear();
}

void SecureBytes::resize(size_t size)
{
    if (size < m_data.size())
        cleanse_memory(m_data.data() + size, m_data.size() - size);
    m_data.resize(size);
}

bool constant_time_equals(std::span<uint8_t const> a, std::span<uint8_t const> b)
{
    if (a.size() != b.size())
        return false;
    if (a.empty())
        return true;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool random_bytes(std::span<uint8_t> out)
{
    if (out.empty())
        return true;
    return RAND_bytes(out.data(), static_cast<int>(out.size())) == 1;
}

std::string base64_encode(std::span<uint8_t const> data)
{
    static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16)
            | (static_cast<uint32_t>(data[i + 1]) << 8)
            | static_cast<uint32_t>(data[i + 2]);
        out.push_back(TABLE[(v >> 18) & 0x3F]);
        out.push_back(TABLE[(v >> 12) & 0x3F]);
        out.push_back(TABLE[(v >> 6) & 0x3F]);
        out.push_back(TABLE[v & 0x3F]);
        i += 3;
    }
    if (i < data.size()) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size())
            v |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(TABLE[(v >> 18) & 0x3F]);
        out.push_back(TABLE[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < data.size()) ? TABLE[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

std::string base64url_encode(std::span<uint8_t const> data)
{
    auto b64 = base64_encode(data);
    for (char& ch : b64) {
        if (ch == '+')
            ch = '-';
        else if (ch == '/')
            ch = '_';
    }
    while (!b64.empty() && b64.back() == '=')
        b64.pop_back();
    return b64;
}

std::optional<std::vector<uint8_t>> base64_decode(std::string_view encoded)
{
    auto decode_char = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+' || c == '-')
            return 62;
        if (c == '/' || c == '_')
            return 63;
        return -1;
    };

    // Strip ASCII whitespace.
    std::string clean;
    clean.reserve(encoded.size());
    for (char c : encoded) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        clean.push_back(c);
    }
    if (clean.empty())
        return std::vector<uint8_t> {};
    if (clean.size() % 4 == 1)
        return std::nullopt;
    while (clean.size() % 4 != 0)
        clean.push_back('=');

    std::vector<uint8_t> out;
    out.reserve((clean.size() / 4) * 3);

    for (size_t i = 0; i < clean.size(); i += 4) {
        int a = decode_char(static_cast<unsigned char>(clean[i]));
        int b = decode_char(static_cast<unsigned char>(clean[i + 1]));
        if (a < 0 || b < 0)
            return std::nullopt;

        bool pad_c = clean[i + 2] == '=';
        bool pad_d = clean[i + 3] == '=';
        if (pad_c && !pad_d)
            return std::nullopt;
        if ((pad_c || pad_d) && (i + 4 != clean.size()))
            return std::nullopt;

        int c = pad_c ? 0 : decode_char(static_cast<unsigned char>(clean[i + 2]));
        int d = pad_d ? 0 : decode_char(static_cast<unsigned char>(clean[i + 3]));
        if (c < 0 || d < 0)
            return std::nullopt;

        uint32_t v = (static_cast<uint32_t>(a) << 18)
            | (static_cast<uint32_t>(b) << 12)
            | (static_cast<uint32_t>(c) << 6)
            | static_cast<uint32_t>(d);

        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        if (!pad_c)
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        if (!pad_d)
            out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    return out;
}

namespace {

// Clean-room RFC 7693 BLAKE2b + RFC 9106 Argon2id fallback for OpenSSL < 3.2 (e.g., Ubuntu 24.04 CI verify job).
// When OpenSSL 3.2+ / 3.6.5 is present (full Superbird build), EVP_KDF "ARGON2ID" is used directly.

constexpr uint64_t rotr64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64 - n));
}

constexpr uint64_t load_le64(uint8_t const* p)
{
    return static_cast<uint64_t>(p[0])
        | (static_cast<uint64_t>(p[1]) << 8)
        | (static_cast<uint64_t>(p[2]) << 16)
        | (static_cast<uint64_t>(p[3]) << 24)
        | (static_cast<uint64_t>(p[4]) << 32)
        | (static_cast<uint64_t>(p[5]) << 40)
        | (static_cast<uint64_t>(p[6]) << 48)
        | (static_cast<uint64_t>(p[7]) << 56);
}

constexpr void store_le64(uint8_t* p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

constexpr void store_le32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

class Blake2b {
public:
    explicit Blake2b(size_t out_len)
        : m_out_len(out_len)
    {
        static constexpr uint64_t IV[8] = {
            0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
        };
        for (int i = 0; i < 8; ++i)
            m_h[i] = IV[i];
        m_h[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(out_len);
    }

    void update(uint8_t const* data, size_t len)
    {
        while (len > 0) {
            if (m_buf_len == 128) {
                increment_counter(128);
                compress(false);
                m_buf_len = 0;
            }
            size_t take = std::min(len, 128 - m_buf_len);
            std::memcpy(m_buf.data() + m_buf_len, data, take);
            m_buf_len += take;
            data += take;
            len -= take;
        }
    }

    void update_le32(uint32_t v)
    {
        uint8_t b[4];
        store_le32(b, v);
        update(b, 4);
    }

    void final(uint8_t* out)
    {
        increment_counter(m_buf_len);
        std::memset(m_buf.data() + m_buf_len, 0, 128 - m_buf_len);
        compress(true);
        uint8_t full[64];
        for (int i = 0; i < 8; ++i)
            store_le64(full + i * 8, m_h[i]);
        std::memcpy(out, full, m_out_len);
        cleanse_memory(full, sizeof(full));
        cleanse_memory(m_buf.data(), m_buf.size());
        cleanse_memory(m_h, sizeof(m_h));
    }

private:
    void increment_counter(size_t inc)
    {
        m_t[0] += inc;
        if (m_t[0] < inc)
            ++m_t[1];
    }

    void compress(bool is_last)
    {
        static constexpr uint64_t IV[8] = {
            0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
        };
        static constexpr uint8_t SIGMA[12][16] = {
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
            { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
            { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
            { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
            { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
            { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
            { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
            { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
            { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
            { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
            { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
        };

        uint64_t m[16];
        for (int i = 0; i < 16; ++i)
            m[i] = load_le64(m_buf.data() + i * 8);

        uint64_t v[16];
        for (int i = 0; i < 8; ++i) {
            v[i] = m_h[i];
            v[i + 8] = IV[i];
        }
        v[12] ^= m_t[0];
        v[13] ^= m_t[1];
        if (is_last)
            v[14] = ~v[14];

        auto G = [&](int a, int b, int c, int d, uint64_t x, uint64_t y) {
            v[a] = v[a] + v[b] + x;
            v[d] = rotr64(v[d] ^ v[a], 32);
            v[c] = v[c] + v[d];
            v[b] = rotr64(v[b] ^ v[c], 24);
            v[a] = v[a] + v[b] + y;
            v[d] = rotr64(v[d] ^ v[a], 16);
            v[c] = v[c] + v[d];
            v[b] = rotr64(v[b] ^ v[c], 63);
        };

        for (int r = 0; r < 12; ++r) {
            auto const* s = SIGMA[r];
            G(0, 4, 8, 12, m[s[0]], m[s[1]]);
            G(1, 5, 9, 13, m[s[2]], m[s[3]]);
            G(2, 6, 10, 14, m[s[4]], m[s[5]]);
            G(3, 7, 11, 15, m[s[6]], m[s[7]]);
            G(0, 5, 10, 15, m[s[8]], m[s[9]]);
            G(1, 6, 11, 12, m[s[10]], m[s[11]]);
            G(2, 7, 8, 13, m[s[12]], m[s[13]]);
            G(3, 4, 9, 14, m[s[14]], m[s[15]]);
        }
        for (int i = 0; i < 8; ++i)
            m_h[i] ^= v[i] ^ v[i + 8];
    }

    uint64_t m_h[8] {};
    uint64_t m_t[2] { 0, 0 };
    std::array<uint8_t, 128> m_buf {};
    size_t m_buf_len { 0 };
    size_t m_out_len { 64 };
};

void blake2b_long(uint8_t* out, size_t out_len, uint8_t const* in, size_t in_len)
{
    if (out_len <= 64) {
        Blake2b b(out_len);
        b.update_le32(static_cast<uint32_t>(out_len));
        b.update(in, in_len);
        b.final(out);
        return;
    }
    uint8_t v[64];
    {
        Blake2b b(64);
        b.update_le32(static_cast<uint32_t>(out_len));
        b.update(in, in_len);
        b.final(v);
    }
    std::memcpy(out, v, 32);
    out += 32;
    size_t remaining = out_len - 32;
    while (remaining > 64) {
        uint8_t next[64];
        Blake2b b(64);
        b.update(v, 64);
        b.final(next);
        std::memcpy(v, next, 64);
        std::memcpy(out, v, 32);
        out += 32;
        remaining -= 32;
    }
    Blake2b b(remaining);
    b.update(v, 64);
    b.final(out);
    cleanse_memory(v, sizeof(v));
}

struct Argon2Block {
    uint64_t v[128] {};
};

inline uint64_t fBlaMka(uint64_t x, uint64_t y)
{
    uint64_t xy = (x & 0xFFFFFFFFULL) * (y & 0xFFFFFFFFULL);
    return x + y + 2 * xy;
}

inline void argon2_G(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d)
{
    a = fBlaMka(a, b);
    d = rotr64(d ^ a, 32);
    c = fBlaMka(c, d);
    b = rotr64(b ^ c, 24);
    a = fBlaMka(a, b);
    d = rotr64(d ^ a, 16);
    c = fBlaMka(c, d);
    b = rotr64(b ^ c, 63);
}

inline void blake2_round(
    uint64_t& v0, uint64_t& v1, uint64_t& v2, uint64_t& v3,
    uint64_t& v4, uint64_t& v5, uint64_t& v6, uint64_t& v7,
    uint64_t& v8, uint64_t& v9, uint64_t& v10, uint64_t& v11,
    uint64_t& v12, uint64_t& v13, uint64_t& v14, uint64_t& v15)
{
    argon2_G(v0, v4, v8, v12);
    argon2_G(v1, v5, v9, v13);
    argon2_G(v2, v6, v10, v14);
    argon2_G(v3, v7, v11, v15);
    argon2_G(v0, v5, v10, v15);
    argon2_G(v1, v6, v11, v12);
    argon2_G(v2, v7, v8, v13);
    argon2_G(v3, v4, v9, v14);
}

void argon2_fill_block(Argon2Block const& prev, Argon2Block const& ref, Argon2Block& next, bool with_xor)
{
    Argon2Block blockR;
    for (int i = 0; i < 128; ++i)
        blockR.v[i] = prev.v[i] ^ ref.v[i];
    Argon2Block blockTmp = blockR;
    if (with_xor) {
        for (int i = 0; i < 128; ++i)
            blockTmp.v[i] ^= next.v[i];
    }

    for (int i = 0; i < 8; ++i) {
        int o = 16 * i;
        blake2_round(
            blockR.v[o + 0], blockR.v[o + 1], blockR.v[o + 2], blockR.v[o + 3],
            blockR.v[o + 4], blockR.v[o + 5], blockR.v[o + 6], blockR.v[o + 7],
            blockR.v[o + 8], blockR.v[o + 9], blockR.v[o + 10], blockR.v[o + 11],
            blockR.v[o + 12], blockR.v[o + 13], blockR.v[o + 14], blockR.v[o + 15]);
    }
    for (int i = 0; i < 8; ++i) {
        int o = 2 * i;
        blake2_round(
            blockR.v[o + 0], blockR.v[o + 1], blockR.v[o + 16], blockR.v[o + 17],
            blockR.v[o + 32], blockR.v[o + 33], blockR.v[o + 48], blockR.v[o + 49],
            blockR.v[o + 64], blockR.v[o + 65], blockR.v[o + 80], blockR.v[o + 81],
            blockR.v[o + 96], blockR.v[o + 97], blockR.v[o + 112], blockR.v[o + 113]);
    }
    for (int i = 0; i < 128; ++i)
        next.v[i] = blockTmp.v[i] ^ blockR.v[i];
}

std::optional<SecureBytes> argon2id_fallback(
    std::span<uint8_t const> password,
    std::span<uint8_t const> salt,
    uint32_t iterations,
    uint32_t memory_kib,
    uint32_t parallelism,
    size_t output_len)
{
    if (iterations == 0 || parallelism == 0 || output_len < 4 || memory_kib < 8 * parallelism)
        return std::nullopt;

    constexpr uint32_t SYNC_POINTS = 4;
    constexpr uint32_t TYPE_ARGON2ID = 2;
    constexpr uint32_t VERSION = 0x13;

    uint32_t memory_blocks = memory_kib;
    if (memory_blocks < 2 * SYNC_POINTS * parallelism)
        memory_blocks = 2 * SYNC_POINTS * parallelism;
    uint32_t segment_length = memory_blocks / (parallelism * SYNC_POINTS);
    uint32_t lane_length = segment_length * SYNC_POINTS;
    memory_blocks = lane_length * parallelism;

    // H0 = BLAKE2b-512(p, T, m, t, v, y, len(P), P, len(S), S, len(K)=0, len(X)=0)
    uint8_t h0[72] {};
    {
        Blake2b b(64);
        b.update_le32(parallelism);
        b.update_le32(static_cast<uint32_t>(output_len));
        b.update_le32(memory_kib);
        b.update_le32(iterations);
        b.update_le32(VERSION);
        b.update_le32(TYPE_ARGON2ID);
        b.update_le32(static_cast<uint32_t>(password.size()));
        if (!password.empty())
            b.update(password.data(), password.size());
        b.update_le32(static_cast<uint32_t>(salt.size()));
        if (!salt.empty())
            b.update(salt.data(), salt.size());
        b.update_le32(0); // secret length
        b.update_le32(0); // associated data length
        b.final(h0);
    }

    std::vector<Argon2Block> mem(memory_blocks);
    uint8_t block_bytes[1024];
    for (uint32_t l = 0; l < parallelism; ++l) {
        store_le32(h0 + 64, 0);
        store_le32(h0 + 68, l);
        blake2b_long(block_bytes, 1024, h0, 72);
        for (int i = 0; i < 128; ++i)
            mem[l * lane_length + 0].v[i] = load_le64(block_bytes + i * 8);

        store_le32(h0 + 64, 1);
        blake2b_long(block_bytes, 1024, h0, 72);
        for (int i = 0; i < 128; ++i)
            mem[l * lane_length + 1].v[i] = load_le64(block_bytes + i * 8);
    }
    cleanse_memory(h0, sizeof(h0));
    cleanse_memory(block_bytes, sizeof(block_bytes));

    Argon2Block zero_block {};
    auto index_alpha = [&](uint32_t pass, uint32_t slice, uint32_t index, uint32_t pseudo_rand, bool same_lane) -> uint32_t {
        uint32_t reference_area_size = 0;
        if (pass == 0) {
            if (slice == 0) {
                reference_area_size = index - 1;
            } else if (same_lane) {
                reference_area_size = slice * segment_length + index - 1;
            } else {
                reference_area_size = slice * segment_length + (index == 0 ? -1 : 0);
            }
        } else {
            if (same_lane) {
                reference_area_size = lane_length - segment_length + index - 1;
            } else {
                reference_area_size = lane_length - segment_length + (index == 0 ? -1 : 0);
            }
        }

        uint64_t rel = pseudo_rand;
        rel = (rel * rel) >> 32;
        rel = reference_area_size - 1 - ((reference_area_size * rel) >> 32);

        uint32_t start_position = 0;
        if (pass != 0)
            start_position = (slice == SYNC_POINTS - 1) ? 0 : (slice + 1) * segment_length;
        return static_cast<uint32_t>((start_position + rel) % lane_length);
    };

    for (uint32_t pass = 0; pass < iterations; ++pass) {
        for (uint32_t slice = 0; slice < SYNC_POINTS; ++slice) {
            for (uint32_t lane = 0; lane < parallelism; ++lane) {
                bool data_independent = (pass == 0 && slice < SYNC_POINTS / 2);
                Argon2Block input_block {};
                Argon2Block address_block {};
                if (data_independent) {
                    input_block.v[0] = pass;
                    input_block.v[1] = lane;
                    input_block.v[2] = slice;
                    input_block.v[3] = memory_blocks;
                    input_block.v[4] = iterations;
                    input_block.v[5] = TYPE_ARGON2ID;
                }

                uint32_t start_index = (pass == 0 && slice == 0) ? 2 : 0;
                uint32_t curr_offset = lane * lane_length + slice * segment_length + start_index;
                uint32_t prev_offset = (curr_offset % lane_length == 0) ? (curr_offset + lane_length - 1) : (curr_offset - 1);

                for (uint32_t i = start_index; i < segment_length; ++i) {
                    if (curr_offset % lane_length == 1)
                        prev_offset = curr_offset - 1;

                    uint64_t pseudo_rand = 0;
                    if (data_independent) {
                        if (i % 128 == 0 || (i == start_index && start_index > 0)) {
                            input_block.v[6] = (i / 128) + 1;
                            Argon2Block tmp {};
                            argon2_fill_block(zero_block, input_block, tmp, false);
                            argon2_fill_block(zero_block, tmp, address_block, false);
                        }
                        pseudo_rand = address_block.v[i % 128];
                    } else {
                        pseudo_rand = mem[prev_offset].v[0];
                    }

                    uint32_t ref_lane = static_cast<uint32_t>((pseudo_rand >> 32) % parallelism);
                    if (pass == 0 && slice == 0)
                        ref_lane = lane;

                    uint32_t ref_index = index_alpha(pass, slice, i, static_cast<uint32_t>(pseudo_rand & 0xFFFFFFFFULL), ref_lane == lane);
                    Argon2Block const& ref_block = mem[ref_lane * lane_length + ref_index];
                    Argon2Block const& prev_block = mem[prev_offset];
                    argon2_fill_block(prev_block, ref_block, mem[curr_offset], pass != 0);

                    prev_offset = curr_offset;
                    ++curr_offset;
                }
            }
        }
    }

    Argon2Block final_block = mem[lane_length - 1];
    for (uint32_t l = 1; l < parallelism; ++l) {
        for (int i = 0; i < 128; ++i)
            final_block.v[i] ^= mem[l * lane_length + lane_length - 1].v[i];
    }

    uint8_t final_bytes[1024];
    for (int i = 0; i < 128; ++i)
        store_le64(final_bytes + i * 8, final_block.v[i]);

    SecureBytes result(output_len);
    blake2b_long(result.data(), output_len, final_bytes, 1024);

    cleanse_memory(final_bytes, sizeof(final_bytes));
    cleanse_memory(&final_block, sizeof(final_block));
    cleanse_memory(mem.data(), mem.size() * sizeof(Argon2Block));
    return result;
}

} // namespace

std::optional<SecureBytes> derive_argon2id(
    std::span<uint8_t const> password,
    std::span<uint8_t const> salt,
    uint32_t iterations,
    uint32_t memory_kib,
    uint32_t parallelism,
    size_t output_len)
{
    if (iterations == 0 || parallelism == 0 || output_len == 0 || memory_kib < 8 * parallelism)
        return std::nullopt;

#if defined(OSSL_KDF_PARAM_ARGON2_MEMCOST) && defined(OSSL_KDF_PARAM_ARGON2_LANES)
    using KdfPtr = std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)>;
    using CtxPtr = std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)>;

    KdfPtr kdf(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr), &EVP_KDF_free);
    if (kdf) {
        CtxPtr ctx(EVP_KDF_CTX_new(kdf.get()), &EVP_KDF_CTX_free);
        if (!ctx)
            return std::nullopt;

        uint32_t threads = 1;
        uint32_t iter = iterations;
        uint32_t memcost = memory_kib;
        uint32_t lanes = parallelism;
        uint32_t size = static_cast<uint32_t>(output_len);

        // OpenSSL OSSL_PARAM_construct_octet_string takes non-const void* even though it only reads.
        std::vector<uint8_t> pass_copy(password.begin(), password.end());
        std::vector<uint8_t> salt_copy(salt.begin(), salt.end());

        OSSL_PARAM params[8];
        int p = 0;
        params[p++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads);
        params[p++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes);
        params[p++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memcost);
        params[p++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iter);
        params[p++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SIZE, &size);
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, pass_copy.data(), pass_copy.size());
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt_copy.data(), salt_copy.size());
        params[p++] = OSSL_PARAM_construct_end();

        SecureBytes out(output_len);
        int ok = EVP_KDF_derive(ctx.get(), out.data(), out.size(), params);
        cleanse_memory(pass_copy.data(), pass_copy.size());
        cleanse_memory(salt_copy.data(), salt_copy.size());
        if (ok == 1)
            return out;
        return std::nullopt;
    }
#endif

    return argon2id_fallback(password, salt, iterations, memory_kib, parallelism, output_len);
}

std::optional<BitwardenSymmetricKey> BitwardenSymmetricKey::from_bytes(std::span<uint8_t const> bytes64)
{
    if (bytes64.size() != 64)
        return std::nullopt;
    BitwardenSymmetricKey key;
    key.enc_key = SecureBytes(bytes64.subspan(0, 32));
    key.mac_key = SecureBytes(bytes64.subspan(32, 32));
    return key;
}

SecureBytes BitwardenSymmetricKey::to_bytes() const
{
    if (!is_valid())
        return {};
    SecureBytes out(64);
    std::memcpy(out.data(), enc_key.data(), 32);
    std::memcpy(out.data() + 32, mac_key.data(), 32);
    return out;
}

std::string BitwardenCrypto::normalize_email(std::string_view email)
{
    size_t start = 0;
    while (start < email.size() && std::isspace(static_cast<unsigned char>(email[start])))
        ++start;
    size_t end = email.size();
    while (end > start && std::isspace(static_cast<unsigned char>(email[end - 1])))
        --end;

    std::string out;
    out.reserve(end - start);
    for (size_t i = start; i < end; ++i)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(email[i]))));
    return out;
}

std::string BitwardenCrypto::auth_email_header(std::string_view email)
{
    auto norm = normalize_email(email);
    return base64url_encode({ reinterpret_cast<uint8_t const*>(norm.data()), norm.size() });
}

std::optional<SecureBytes> BitwardenCrypto::derive_master_key(
    std::string_view password,
    std::string_view email,
    BitwardenKdfConfig const& config)
{
    auto norm_email = normalize_email(email);
    if (norm_email.empty() || config.iterations == 0)
        return std::nullopt;

    if (config.type == BitwardenKdfType::Pbkdf2Sha256) {
        SecureBytes master_key(32);
        int ok = PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            reinterpret_cast<unsigned char const*>(norm_email.data()),
            static_cast<int>(norm_email.size()),
            static_cast<int>(config.iterations),
            EVP_sha256(),
            32,
            master_key.data());
        if (ok != 1)
            return std::nullopt;
        return master_key;
    }

    if (config.type == BitwardenKdfType::Argon2id) {
        // Bitwarden Argon2id salt = SHA256(normalize_email(email)).
        uint8_t salt_hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<unsigned char const*>(norm_email.data()), norm_email.size(), salt_hash);
        if (config.memory_mb == 0 || config.memory_mb > 4096 || config.parallelism == 0)
            return std::nullopt;
        uint32_t memory_kib = config.memory_mb * 1024;
        return derive_argon2id(
            { reinterpret_cast<uint8_t const*>(password.data()), password.size() },
            { salt_hash, SHA256_DIGEST_LENGTH },
            config.iterations,
            memory_kib,
            config.parallelism,
            32);
    }

    return std::nullopt;
}

std::optional<std::string> BitwardenCrypto::derive_master_password_hash(
    std::span<uint8_t const> master_key,
    std::string_view password)
{
    if (master_key.size() != 32)
        return std::nullopt;
    SecureBytes hash(32);
    int ok = PKCS5_PBKDF2_HMAC(
        reinterpret_cast<char const*>(master_key.data()),
        static_cast<int>(master_key.size()),
        reinterpret_cast<unsigned char const*>(password.data()),
        static_cast<int>(password.size()),
        1,
        EVP_sha256(),
        32,
        hash.data());
    if (ok != 1)
        return std::nullopt;
    return base64_encode(hash.span());
}

std::optional<SecureBytes> BitwardenCrypto::hkdf_expand_sha256(
    std::span<uint8_t const> prk,
    std::string_view info,
    size_t output_len)
{
    if (prk.empty() || output_len == 0 || output_len > 255 * 32)
        return std::nullopt;

    SecureBytes out;
    out.resize(output_len);

    uint8_t prev[32] {};
    size_t prev_len = 0;
    size_t produced = 0;
    uint8_t counter = 1;

    while (produced < output_len) {
        std::vector<uint8_t> msg;
        msg.reserve(prev_len + info.size() + 1);
        msg.insert(msg.end(), prev, prev + prev_len);
        msg.insert(msg.end(), info.begin(), info.end());
        msg.push_back(counter);

        unsigned int mac_len = 0;
        uint8_t block[32] {};
        unsigned char* res = HMAC(
            EVP_sha256(),
            prk.data(),
            static_cast<int>(prk.size()),
            msg.data(),
            msg.size(),
            block,
            &mac_len);
        cleanse_memory(msg.data(), msg.size());
        if (!res || mac_len != 32) {
            cleanse_memory(prev, sizeof(prev));
            cleanse_memory(block, sizeof(block));
            return std::nullopt;
        }

        size_t take = std::min(static_cast<size_t>(32), output_len - produced);
        std::memcpy(out.data() + produced, block, take);
        std::memcpy(prev, block, 32);
        prev_len = 32;
        produced += take;
        ++counter;
        cleanse_memory(block, sizeof(block));
    }
    cleanse_memory(prev, sizeof(prev));
    return out;
}

std::optional<BitwardenSymmetricKey> BitwardenCrypto::stretch_master_key(std::span<uint8_t const> master_key)
{
    if (master_key.size() != 32)
        return std::nullopt;
    auto enc = hkdf_expand_sha256(master_key, "enc", 32);
    auto mac = hkdf_expand_sha256(master_key, "mac", 32);
    if (!enc || !mac)
        return std::nullopt;
    BitwardenSymmetricKey stretched;
    stretched.enc_key = std::move(*enc);
    stretched.mac_key = std::move(*mac);
    return stretched;
}

std::optional<BitwardenCipherString> BitwardenCrypto::parse_cipher_string(std::string_view cipher_string)
{
    auto dot = cipher_string.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= cipher_string.size())
        return std::nullopt;

    auto type_str = cipher_string.substr(0, dot);
    int type_num = 0;
    for (char c : type_str) {
        if (c < '0' || c > '9')
            return std::nullopt;
        type_num = type_num * 10 + (c - '0');
    }

    auto body = cipher_string.substr(dot + 1);
    std::vector<std::string_view> parts;
    size_t pos = 0;
    while (true) {
        auto bar = body.find('|', pos);
        if (bar == std::string_view::npos) {
            parts.push_back(body.substr(pos));
            break;
        }
        parts.push_back(body.substr(pos, bar - pos));
        pos = bar + 1;
    }

    BitwardenCipherString parsed;
    switch (type_num) {
    case 0: {
        if (parts.size() != 2)
            return std::nullopt;
        auto iv = base64_decode(parts[0]);
        auto ct = base64_decode(parts[1]);
        if (!iv || !ct || iv->size() != 16)
            return std::nullopt;
        parsed.type = BitwardenCipherType::AesCbc256_B64;
        parsed.iv = std::move(*iv);
        parsed.ciphertext = std::move(*ct);
        return parsed;
    }
    case 2: {
        if (parts.size() != 3)
            return std::nullopt;
        auto iv = base64_decode(parts[0]);
        auto ct = base64_decode(parts[1]);
        auto mac = base64_decode(parts[2]);
        if (!iv || !ct || !mac || iv->size() != 16 || mac->size() != 32)
            return std::nullopt;
        parsed.type = BitwardenCipherType::AesCbc256_HmacSha256_B64;
        parsed.iv = std::move(*iv);
        parsed.ciphertext = std::move(*ct);
        parsed.mac = std::move(*mac);
        return parsed;
    }
    case 3:
    case 4: {
        if (parts.size() != 1)
            return std::nullopt;
        auto ct = base64_decode(parts[0]);
        if (!ct || ct->empty())
            return std::nullopt;
        parsed.type = type_num == 3 ? BitwardenCipherType::Rsa2048_OaepSha256_B64 : BitwardenCipherType::Rsa2048_OaepSha1_B64;
        parsed.ciphertext = std::move(*ct);
        return parsed;
    }
    case 5:
    case 6: {
        if (parts.size() != 2)
            return std::nullopt;
        auto ct = base64_decode(parts[0]);
        auto mac = base64_decode(parts[1]);
        if (!ct || !mac || ct->empty() || mac->size() != 32)
            return std::nullopt;
        parsed.type = type_num == 5 ? BitwardenCipherType::Rsa2048_OaepSha256_HmacSha256_B64 : BitwardenCipherType::Rsa2048_OaepSha1_HmacSha256_B64;
        parsed.ciphertext = std::move(*ct);
        parsed.mac = std::move(*mac);
        return parsed;
    }
    default:
        return std::nullopt;
    }
}

std::optional<SecureBytes> BitwardenCrypto::decrypt_bytes(
    std::string_view cipher_string,
    BitwardenSymmetricKey const& key)
{
    if (!key.is_valid())
        return std::nullopt;

    auto parsed = parse_cipher_string(cipher_string);
    if (!parsed)
        return std::nullopt;

    if (parsed->type != BitwardenCipherType::AesCbc256_HmacSha256_B64
        && parsed->type != BitwardenCipherType::AesCbc256_B64)
        return std::nullopt;

    // Authenticate-then-decrypt with constant-time MAC comparison for Type 2.
    if (parsed->type == BitwardenCipherType::AesCbc256_HmacSha256_B64) {
        std::vector<uint8_t> mac_data;
        mac_data.reserve(parsed->iv.size() + parsed->ciphertext.size());
        mac_data.insert(mac_data.end(), parsed->iv.begin(), parsed->iv.end());
        mac_data.insert(mac_data.end(), parsed->ciphertext.begin(), parsed->ciphertext.end());

        uint8_t computed_mac[32] {};
        unsigned int computed_len = 0;
        unsigned char* res = HMAC(
            EVP_sha256(),
            key.mac_key.data(),
            static_cast<int>(key.mac_key.size()),
            mac_data.data(),
            mac_data.size(),
            computed_mac,
            &computed_len);
        if (!res || computed_len != 32)
            return std::nullopt;
        if (!constant_time_equals({ computed_mac, 32 }, parsed->mac))
            return std::nullopt;
    }

    if (parsed->ciphertext.empty() || (parsed->ciphertext.size() % 16) != 0)
        return std::nullopt;

    using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, key.enc_key.data(), parsed->iv.data()) != 1)
        return std::nullopt;

    SecureBytes plaintext(parsed->ciphertext.size() + 16);
    int out_len1 = 0;
    if (EVP_DecryptUpdate(
            ctx.get(),
            plaintext.data(),
            &out_len1,
            parsed->ciphertext.data(),
            static_cast<int>(parsed->ciphertext.size()))
        != 1)
        return std::nullopt;

    int out_len2 = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + out_len1, &out_len2) != 1)
        return std::nullopt;

    plaintext.resize(static_cast<size_t>(out_len1 + out_len2));
    return plaintext;
}

std::optional<std::string> BitwardenCrypto::decrypt_string(
    std::string_view cipher_string,
    BitwardenSymmetricKey const& key)
{
    auto bytes = decrypt_bytes(cipher_string, key);
    if (!bytes)
        return std::nullopt;
    return std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
}

std::optional<std::string> BitwardenCrypto::encrypt_bytes(
    std::span<uint8_t const> plaintext,
    BitwardenSymmetricKey const& key,
    std::optional<std::array<uint8_t, 16>> deterministic_iv)
{
    if (!key.is_valid())
        return std::nullopt;

    std::array<uint8_t, 16> iv {};
    if (deterministic_iv.has_value()) {
        iv = *deterministic_iv;
    } else if (!random_bytes(iv)) {
        return std::nullopt;
    }

    using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, key.enc_key.data(), iv.data()) != 1)
        return std::nullopt;

    std::vector<uint8_t> ciphertext(plaintext.size() + 16);
    int out_len1 = 0;
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(
                ctx.get(),
                ciphertext.data(),
                &out_len1,
                plaintext.data(),
                static_cast<int>(plaintext.size()))
            != 1)
            return std::nullopt;
    }

    int out_len2 = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + out_len1, &out_len2) != 1)
        return std::nullopt;
    ciphertext.resize(static_cast<size_t>(out_len1 + out_len2));

    std::vector<uint8_t> mac_data;
    mac_data.reserve(iv.size() + ciphertext.size());
    mac_data.insert(mac_data.end(), iv.begin(), iv.end());
    mac_data.insert(mac_data.end(), ciphertext.begin(), ciphertext.end());

    uint8_t mac[32] {};
    unsigned int mac_len = 0;
    unsigned char* res = HMAC(
        EVP_sha256(),
        key.mac_key.data(),
        static_cast<int>(key.mac_key.size()),
        mac_data.data(),
        mac_data.size(),
        mac,
        &mac_len);
    if (!res || mac_len != 32)
        return std::nullopt;

    return "2." + base64_encode(iv) + "|" + base64_encode(ciphertext) + "|" + base64_encode({ mac, 32 });
}

std::optional<std::string> BitwardenCrypto::encrypt_string(
    std::string_view plaintext,
    BitwardenSymmetricKey const& key)
{
    return encrypt_bytes(
        { reinterpret_cast<uint8_t const*>(plaintext.data()), plaintext.size() },
        key);
}

std::optional<SecureBytes> BitwardenCrypto::rsa_decrypt(
    std::string_view cipher_string,
    std::span<uint8_t const> pkcs8_private_key_der)
{
    auto parsed = parse_cipher_string(cipher_string);
    if (!parsed || pkcs8_private_key_der.empty())
        return std::nullopt;

    EVP_MD const* oaep_md = nullptr;
    switch (parsed->type) {
    case BitwardenCipherType::Rsa2048_OaepSha256_B64:
    case BitwardenCipherType::Rsa2048_OaepSha256_HmacSha256_B64:
        oaep_md = EVP_sha256();
        break;
    case BitwardenCipherType::Rsa2048_OaepSha1_B64:
    case BitwardenCipherType::Rsa2048_OaepSha1_HmacSha256_B64:
        oaep_md = EVP_sha1();
        break;
    default:
        return std::nullopt;
    }

    unsigned char const* p = pkcs8_private_key_der.data();
    using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    PkeyPtr pkey(d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(pkcs8_private_key_der.size())), &EVP_PKEY_free);
    if (!pkey)
        return std::nullopt;

    using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    PkeyCtxPtr ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr), &EVP_PKEY_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_PKEY_decrypt_init(ctx.get()) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), oaep_md) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), oaep_md) <= 0)
        return std::nullopt;

    size_t out_len = 0;
    if (EVP_PKEY_decrypt(ctx.get(), nullptr, &out_len, parsed->ciphertext.data(), parsed->ciphertext.size()) <= 0)
        return std::nullopt;

    SecureBytes out(out_len);
    if (EVP_PKEY_decrypt(ctx.get(), out.data(), &out_len, parsed->ciphertext.data(), parsed->ciphertext.size()) <= 0)
        return std::nullopt;
    out.resize(out_len);
    return out;
}

std::optional<std::string> BitwardenCrypto::rsa_encrypt(
    std::span<uint8_t const> plaintext,
    std::span<uint8_t const> spki_public_key_der,
    BitwardenCipherType type)
{
    EVP_MD const* oaep_md = nullptr;
    int prefix = 4;
    if (type == BitwardenCipherType::Rsa2048_OaepSha256_B64) {
        oaep_md = EVP_sha256();
        prefix = 3;
    } else if (type == BitwardenCipherType::Rsa2048_OaepSha1_B64) {
        oaep_md = EVP_sha1();
        prefix = 4;
    } else {
        return std::nullopt;
    }

    unsigned char const* p = spki_public_key_der.data();
    using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    PkeyPtr pkey(d2i_PUBKEY(nullptr, &p, static_cast<long>(spki_public_key_der.size())), &EVP_PKEY_free);
    if (!pkey)
        return std::nullopt;

    using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    PkeyCtxPtr ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr), &EVP_PKEY_CTX_free);
    if (!ctx)
        return std::nullopt;

    if (EVP_PKEY_encrypt_init(ctx.get()) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), oaep_md) <= 0)
        return std::nullopt;
    if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), oaep_md) <= 0)
        return std::nullopt;

    size_t out_len = 0;
    if (EVP_PKEY_encrypt(ctx.get(), nullptr, &out_len, plaintext.data(), plaintext.size()) <= 0)
        return std::nullopt;

    std::vector<uint8_t> ct(out_len);
    if (EVP_PKEY_encrypt(ctx.get(), ct.data(), &out_len, plaintext.data(), plaintext.size()) <= 0)
        return std::nullopt;
    ct.resize(out_len);

    return std::to_string(prefix) + "." + base64_encode(ct);
}

std::optional<BitwardenSymmetricKey> BitwardenCrypto::generate_symmetric_key()
{
    SecureBytes raw(64);
    if (!random_bytes(raw.span()))
        return std::nullopt;
    return BitwardenSymmetricKey::from_bytes(raw.span());
}

}
