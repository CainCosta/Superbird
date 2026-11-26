#!/usr/bin/env node
/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Converts a uBlock Origin source tree (fetched at runtime, never vendored in Superbird)
 * into the adblock-rust Resource[] JSON format consumed by Superbird's uBO-compatible engine.
 */

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";

const DEFAULT_UBO_TAG = "1.77.2";
const DEFAULT_UBO_REPO = "https://github.com/gorhill/uBlock.git";

const FUNCTION_DECL_RE = /^function\s+([^\(\)\{\}\s]+)\s*\(/;

function parseArgs(argv) {
    const args = {
        tag: DEFAULT_UBO_TAG,
        repo: DEFAULT_UBO_REPO,
        uboDir: null,
        out: null,
        stats: false,
    };
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === "--tag" && i + 1 < argv.length) {
            args.tag = argv[++i];
        } else if (arg === "--repo" && i + 1 < argv.length) {
            args.repo = argv[++i];
        } else if (arg === "--ubo-dir" && i + 1 < argv.length) {
            args.uboDir = argv[++i];
        } else if (arg === "--out" && i + 1 < argv.length) {
            args.out = argv[++i];
        } else if (arg === "--stats") {
            args.stats = true;
        } else if (arg === "-h" || arg === "--help") {
            console.log(
                "Usage: build-ubo-resources.mjs --out <path> [--tag <ubo-tag>] [--ubo-dir <local-clone>] [--stats]"
            );
            process.exit(0);
        } else {
            throw new Error(`Unknown argument: ${arg}`);
        }
    }
    if (!args.out) {
        throw new Error("--out <path> is required");
    }
    return args;
}

function sanitizeIdentifier(name) {
    let id = name.replace(/[^A-Za-z0-9_$]/g, "_");
    if (!/^[A-Za-z_$]/.test(id)) {
        id = `fn_${id}`;
    }
    return id;
}

function normalizeFunctionSource(fn, resourceName) {
    const raw = fn.toString().trim();
    if (FUNCTION_DECL_RE.test(raw)) {
        return raw;
    }
    const safeName = sanitizeIdentifier(resourceName.replace(/\.(js|fn)$/, ""));
    if (/^function\s*\(/.test(raw)) {
        return raw.replace(/^function\s*\(/, `function ${safeName}(`);
    }
    if (/^async\s+function\s+([^\(\)\{\}\s]+)\s*\(/.test(raw)) {
        return `function ${safeName}(...args) { return (${raw})(...args); }`;
    }
    return `function ${safeName}(...args) { return (${raw})(...args); }`;
}

function inferMimeType(name, declaredMime) {
    if (declaredMime) {
        const clean = declaredMime.split(";")[0].trim().toLowerCase();
        if (clean) return clean;
    }
    const ext = path.extname(name).toLowerCase();
    switch (ext) {
        case ".js":
            return "application/javascript";
        case ".css":
            return "text/css";
        case ".html":
            return "text/html";
        case ".xml":
            return "text/xml";
        case ".json":
            return "application/json";
        case ".gif":
            return "image/gif";
        case ".png":
            return "image/png";
        case ".svg":
            return "image/svg+xml";
        case ".mp3":
            return "audio/mp3";
        case ".mp4":
            return "video/mp4";
        case ".txt":
        default:
            return "text/plain";
    }
}

function isTrustedName(name) {
    return typeof name === "string" && name.startsWith("trusted-");
}

function ensureArray(val) {
    if (!val) return [];
    return Array.isArray(val) ? val : [val];
}

async function convertUboDirectory(uboDir) {
    const scriptletsPath = path.join(uboDir, "src/js/resources/scriptlets.js");
    const redirectMapPath = path.join(uboDir, "src/js/redirect-resources.js");
    const warDir = path.join(uboDir, "src/web_accessible_resources");

    if (!fs.existsSync(scriptletsPath)) {
        throw new Error(`Missing ${scriptletsPath}`);
    }

    const scriptletsMod = await import(pathToFileURL(scriptletsPath).href);
    const builtinScriptlets = scriptletsMod.builtinScriptlets || [];

    const resourcesByName = new Map();
    const usedIdentifiers = new Set();

    function claimAliases(primaryName, candidates) {
        const claimed = [];
        for (const candidate of candidates) {
            if (!candidate || candidate === primaryName) continue;
            if (usedIdentifiers.has(candidate)) continue;
            usedIdentifiers.add(candidate);
            claimed.push(candidate);
        }
        return claimed;
    }

    // Pass 1: Reserve all canonical scriptlet / helper names first so aliases never shadow a canonical resource.
    for (const entry of builtinScriptlets) {
        if (!entry || !entry.name || typeof entry.fn !== "function") continue;
        usedIdentifiers.add(entry.name);
    }

    // Pass 2: Emit all builtin scriptlets and .fn helpers.
    let scriptletCount = 0;
    let helperCount = 0;
    let trustedCount = 0;

    for (const entry of builtinScriptlets) {
        if (!entry || !entry.name || typeof entry.fn !== "function") continue;

        const isHelper = entry.name.endsWith(".fn");
        const source = normalizeFunctionSource(entry.fn, entry.name);
        if (!FUNCTION_DECL_RE.test(source)) {
            throw new Error(`Failed to normalize function declaration for ${entry.name}`);
        }

        const rawAliases = ensureArray(entry.aliases);
        const expandedAliases = [];
        for (const alias of rawAliases) {
            expandedAliases.push(alias);
            if (!isHelper && !alias.endsWith(".js") && !alias.endsWith(".fn")) {
                expandedAliases.push(`${alias}.js`);
            }
        }
        const aliases = claimAliases(entry.name, expandedAliases);

        const requiresTrust =
            entry.requiresTrust === true ||
            isTrustedName(entry.name) ||
            rawAliases.some(isTrustedName);

        const resource = {
            name: entry.name,
            aliases,
            kind: { mime: isHelper ? "fn/javascript" : "application/javascript" },
            content: Buffer.from(source, "utf8").toString("base64"),
            dependencies: ensureArray(entry.dependencies),
            permission: requiresTrust ? 1 : 0,
        };

        resourcesByName.set(entry.name, resource);
        if (isHelper) helperCount++;
        else scriptletCount++;
        if (requiresTrust) trustedCount++;
    }

    // Pass 3: Emit redirect resources from redirect-resources.js + src/web_accessible_resources/.
    let redirectCount = 0;
    let redirectSkipped = 0;

    if (fs.existsSync(redirectMapPath) && fs.existsSync(warDir)) {
        const redirectMod = await import(pathToFileURL(redirectMapPath).href);
        const redirectMap = redirectMod.default;
        if (redirectMap && typeof redirectMap.entries === "function") {
            // Reserve canonical redirect names first.
            for (const [name] of redirectMap.entries()) {
                if (!resourcesByName.has(name)) {
                    usedIdentifiers.add(name);
                }
            }

            for (const [name, details] of redirectMap.entries()) {
                const rawAliases = ensureArray(details?.alias);
                const existing = resourcesByName.get(name);
                if (existing) {
                    // Already emitted as a scriptlet (e.g. noop.js); merge non-colliding redirect aliases.
                    const extraAliases = claimAliases(name, rawAliases);
                    existing.aliases.push(...extraAliases);
                    continue;
                }

                let contentBuffer = null;
                let mime = null;

                if (details?.data === "text") {
                    contentBuffer = Buffer.from("", "utf8");
                    mime = "text/plain";
                } else if (typeof details?.data === "string" && details.data.startsWith("data:")) {
                    const commaIdx = details.data.indexOf(",");
                    if (commaIdx !== -1) {
                        const meta = details.data.slice(5, commaIdx);
                        const payload = details.data.slice(commaIdx + 1);
                        mime = meta.split(";")[0] || "text/plain";
                        if (meta.includes(";base64")) {
                            contentBuffer = Buffer.from(payload, "base64");
                        } else {
                            contentBuffer = Buffer.from(decodeURIComponent(payload), "utf8");
                        }
                    }
                } else {
                    const warFile = path.join(warDir, name);
                    if (fs.existsSync(warFile) && fs.statSync(warFile).isFile()) {
                        contentBuffer = fs.readFileSync(warFile);
                        mime = inferMimeType(name, null);
                    }
                }

                if (contentBuffer === null) {
                    redirectSkipped++;
                    continue;
                }

                const aliases = claimAliases(name, rawAliases);
                const resource = {
                    name,
                    aliases,
                    kind: { mime: inferMimeType(name, mime) },
                    content: contentBuffer.toString("base64"),
                    dependencies: [],
                    permission: 0,
                };
                resourcesByName.set(name, resource);
                redirectCount++;
            }
        }
    }

    // Validate every dependency exists and no duplicate names/aliases exist.
    const resources = Array.from(resourcesByName.values());
    const finalSeen = new Set();
    for (const res of resources) {
        for (const ident of [res.name, ...res.aliases]) {
            if (finalSeen.has(ident)) {
                throw new Error(`Duplicate resource identifier emitted: ${ident}`);
            }
            finalSeen.add(ident);
        }
        for (const dep of res.dependencies) {
            if (!resourcesByName.has(dep)) {
                throw new Error(`Resource ${res.name} references missing dependency ${dep}`);
            }
        }
    }

    return {
        resources,
        stats: {
            total: resources.length,
            scriptletCount,
            helperCount,
            trustedCount,
            redirectCount,
            redirectSkipped,
        },
    };
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    let tempDir = null;
    let uboDir = args.uboDir;

    try {
        if (!uboDir) {
            tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "superbird-ubo-src-"));
            execFileSync(
                "git",
                ["clone", "--depth", "1", "--branch", args.tag, args.repo, tempDir],
                { stdio: ["ignore", "pipe", "pipe"] }
            );
            uboDir = tempDir;
        }

        const { resources, stats } = await convertUboDirectory(uboDir);
        fs.mkdirSync(path.dirname(path.resolve(args.out)), { recursive: true });
        const tmpOut = `${args.out}.tmp.${process.pid}`;
        fs.writeFileSync(tmpOut, JSON.stringify(resources));
        fs.renameSync(tmpOut, args.out);

        if (args.stats) {
            const bytes = fs.statSync(args.out).size;
            console.log(
                JSON.stringify({
                    tag: args.uboDir ? "local" : args.tag,
                    out: args.out,
                    bytes,
                    ...stats,
                })
            );
        }
    } finally {
        if (tempDir) {
            fs.rmSync(tempDir, { recursive: true, force: true });
        }
    }
}

main().catch((err) => {
    console.error(err?.stack || String(err));
    process.exit(1);
});
