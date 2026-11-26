/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordGenerator.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <vector>

namespace Superbird::Passwords {

namespace {

// Unbiased CSPRNG uniform integer in [0, bound).
size_t csprng_uniform(size_t bound)
{
    if (bound <= 1)
        return 0;
    uint64_t limit = std::numeric_limits<uint64_t>::max() - (std::numeric_limits<uint64_t>::max() % bound);
    uint64_t value = 0;
    do {
        uint8_t buf[8] {};
        if (!random_bytes(buf))
            return 0;
        value = 0;
        for (int i = 0; i < 8; ++i)
            value = (value << 8) | buf[i];
    } while (value >= limit);
    return static_cast<size_t>(value % bound);
}

constexpr std::array<std::string_view, 256> WORDLIST = {
    "acorn", "alder", "amber", "anchor", "anise", "anvil", "apple", "apron",
    "arbor", "arrow", "aspen", "atlas", "attic", "autumn", "badge", "baker",
    "bamboo", "banjo", "barge", "barley", "barrel", "basin", "basket", "beacon",
    "beaver", "beech", "berry", "bison", "blade", "blaze", "bloom", "bluff",
    "bobcat", "boulder", "branch", "brass", "breeze", "bridge", "brook", "brush",
    "bucket", "bugle", "cabin", "cable", "cactus", "cairn", "camel", "canal",
    "candle", "cannon", "canopy", "canyon", "caper", "carob", "carpet", "carrot",
    "castle", "cedar", "celery", "chalk", "chapel", "cherry", "chisel", "cider",
    "cinder", "citrus", "clasp", "clover", "cobalt", "cobble", "cocoa", "comet",
    "compass", "condor", "copper", "coral", "cornet", "cotton", "cougar", "cradle",
    "crane", "crater", "creek", "crest", "cricket", "crocus", "crown", "crystal",
    "cumulus", "cypress", "dagger", "dahlia", "daisy", "dancer", "delta", "desert",
    "dewdrop", "diamond", "dinghy", "dolphin", "domino", "donkey", "dragon", "drift",
    "drizzle", "drum", "eagle", "earth", "ebony", "echo", "eclipse", "egret",
    "elder", "ember", "emerald", "ermine", "estuary", "falcon", "fennel", "ferret",
    "fiddle", "field", "finch", "firefly", "fjord", "flannel", "flint", "flora",
    "flute", "forest", "fossil", "foxglove", "fresco", "frost", "funnel", "galaxy",
    "gannet", "garden", "garnet", "gazebo", "gecko", "geyser", "ginger", "glacier",
    "glade", "gopher", "granite", "gravel", "griffin", "grotto", "grove", "gull",
    "hammer", "harbor", "harvest", "hazel", "heather", "helmet", "heron", "hickory",
    "hilltop", "hollow", "honey", "horizon", "hornet", "husk", "icicle", "iguana",
    "indigo", "inlet", "island", "ivory", "jackal", "jade", "jaguar", "jasmine",
    "jasper", "javelin", "juniper", "kayak", "kelp", "kestrel", "kettle", "kodiak",
    "lagoon", "lantern", "larch", "lark", "laurel", "lavender", "ledge", "lemon",
    "leopard", "lichen", "lilac", "linden", "linnet", "lizard", "locket", "locust",
    "lodge", "lotus", "lumber", "lynx", "magnet", "magpie", "mahogany", "mallard",
    "mango", "mantis", "maple", "marble", "marigold", "marmot", "marsh", "marten",
    "meadow", "melon", "mercury", "mesa", "meteor", "mica", "miller", "mimosa",
    "minnow", "mirage", "mitten", "monarch", "monsoon", "moose", "mortar", "mosaic",
    "moss", "muffin", "mulberry", "muskrat", "myrtle", "narwhal", "nectar", "needle",
    "nettle", "newt", "nickel", "nimbus", "nutmeg", "oakhill", "oasis", "obsidian",
};

} // namespace

std::string PasswordGenerator::generate_password(PasswordGeneratorOptions const& options)
{
    size_t length = std::clamp<size_t>(options.length, 8, 128);

    std::string upper = options.avoid_ambiguous ? "ABCDEFGHJKLMNPQRSTUVWXYZ" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string lower = options.avoid_ambiguous ? "abcdefghijkmnopqrstuvwxyz" : "abcdefghijklmnopqrstuvwxyz";
    std::string digits = options.avoid_ambiguous ? "23456789" : "0123456789";
    std::string symbols = "!@#$%^&*()-_=+[]{}";

    std::vector<std::string_view> enabled_classes;
    if (options.uppercase)
        enabled_classes.push_back(upper);
    if (options.lowercase)
        enabled_classes.push_back(lower);
    if (options.digits)
        enabled_classes.push_back(digits);
    if (options.symbols)
        enabled_classes.push_back(symbols);

    if (enabled_classes.empty())
        enabled_classes.push_back(lower);

    std::string pool;
    for (auto cls : enabled_classes)
        pool.append(cls);

    std::string result;
    result.reserve(length);

    // Guarantee at least one character from each enabled class.
    for (auto cls : enabled_classes) {
        if (result.size() < length)
            result.push_back(cls[csprng_uniform(cls.size())]);
    }

    while (result.size() < length)
        result.push_back(pool[csprng_uniform(pool.size())]);

    // Fisher-Yates shuffle with CSPRNG.
    for (size_t i = result.size() - 1; i > 0; --i) {
        size_t j = csprng_uniform(i + 1);
        std::swap(result[i], result[j]);
    }
    return result;
}

std::string PasswordGenerator::generate_passphrase(PassphraseGeneratorOptions const& options)
{
    size_t count = std::clamp<size_t>(options.word_count, 3, 10);
    size_t number_word = options.include_number ? csprng_uniform(count) : count;

    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (i > 0)
            out.push_back(options.separator);
        std::string word(WORDLIST[csprng_uniform(WORDLIST.size())]);
        if (options.capitalize && !word.empty())
            word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        out.append(word);
        if (i == number_word)
            out.push_back(static_cast<char>('0' + csprng_uniform(10)));
    }
    return out;
}

PasswordStrength PasswordGenerator::estimate_strength(std::string_view password)
{
    if (password.empty())
        return { 0.0, "Weak" };

    bool has_lower = false;
    bool has_upper = false;
    bool has_digit = false;
    bool has_symbol = false;
    for (unsigned char c : password) {
        if (std::islower(c))
            has_lower = true;
        else if (std::isupper(c))
            has_upper = true;
        else if (std::isdigit(c))
            has_digit = true;
        else
            has_symbol = true;
    }

    double charset = 0.0;
    if (has_lower)
        charset += 26.0;
    if (has_upper)
        charset += 26.0;
    if (has_digit)
        charset += 10.0;
    if (has_symbol)
        charset += 32.0;
    if (charset < 1.0)
        charset = 1.0;

    double bits = static_cast<double>(password.size()) * std::log2(charset);
    if (bits < 40.0)
        return { bits, "Weak" };
    if (bits < 65.0)
        return { bits, "Fair" };
    if (bits < 95.0)
        return { bits, "Strong" };
    return { bits, "Very Strong" };
}

}
