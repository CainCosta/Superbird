#!/usr/bin/env node
/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const BUILD_SCRIPT = path.resolve(__dirname, "../scripts/build-ubo-resources.mjs");

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "superbird-test-res-"));
try {
    const mockUbo = path.join(tmpDir, "mock-ubo");
    fs.mkdirSync(path.join(mockUbo, "src/js/resources"), { recursive: true });
    fs.mkdirSync(path.join(mockUbo, "src/web_accessible_resources"), { recursive: true });

    fs.writeFileSync(
        path.join(mockUbo, "src/js/resources/scriptlets.js"),
        `
export const builtinScriptlets = [
    {
        name: "safe-self.fn",
        fn: function safeSelf() { return scriptletGlobals; },
    },
    {
        name: "set-constant.js",
        aliases: ["set.js", "set"],
        dependencies: ["safe-self.fn"],
        fn: function setConstant(k, v) { window[k] = v; },
    },
    {
        // Intentionally omit requiresTrust to verify trusted- prefix defense-in-depth
        name: "trusted-prevent-xhr.js",
        aliases: ["trusted-no-xhr-if.js"],
        fn: (arg) => { void arg; },
    },
    {
        name: "explicit-trust.js",
        requiresTrust: true,
        fn: function(x) { return x; },
    },
    {
        name: "noop.js",
        aliases: ["noopjs"],
        fn: function noop() {},
    },
];
`
    );

    fs.writeFileSync(
        path.join(mockUbo, "src/js/redirect-resources.js"),
        `
export default new Map([
    ["noop.js", { alias: ["noopjs", "noop-extra.js"] }],
    ["1x1.gif", { alias: ["1x1-transparent.gif"] }],
    ["empty", { data: "text" }],
    ["inline.txt", { data: "data:text/plain;base64,aGVsbG8=" }],
]);
`
    );
    fs.writeFileSync(path.join(mockUbo, "src/web_accessible_resources/1x1.gif"), Buffer.from("GIF89a"));
    fs.writeFileSync(path.join(mockUbo, "src/web_accessible_resources/noop.js"), "/* noop */");

    const outPath = path.join(tmpDir, "ubo-resources.json");
    execFileSync("node", [BUILD_SCRIPT, "--ubo-dir", mockUbo, "--out", outPath, "--stats"], {
        stdio: "pipe",
    });

    const resources = JSON.parse(fs.readFileSync(outPath, "utf8"));
    const byName = new Map(resources.map((r) => [r.name, r]));

    // Helper uses fn/javascript
    assert.deepEqual(byName.get("safe-self.fn").kind, { mime: "fn/javascript" });
    assert.equal(byName.get("safe-self.fn").permission, 0);

    // Scriptlet with alias + dependency
    const setConst = byName.get("set-constant.js");
    assert.deepEqual(setConst.kind, { mime: "application/javascript" });
    assert.deepEqual(setConst.dependencies, ["safe-self.fn"]);
    assert.ok(setConst.aliases.includes("set.js"));
    assert.equal(setConst.permission, 0);

    // trusted- prefix MUST enforce permission=1 even when requiresTrust is omitted,
    // and arrow fn MUST be normalized into a named `function ...(` declaration for adblock-rust
    const trustedXhr = byName.get("trusted-prevent-xhr.js");
    assert.equal(trustedXhr.permission, 1);
    const trustedXhrSrc = Buffer.from(trustedXhr.content, "base64").toString("utf8");
    assert.match(trustedXhrSrc, /^function\s+[^\(\)\{\}\s]+\s*\(/);

    // Anonymous function + explicit requiresTrust
    const explicitTrust = byName.get("explicit-trust.js");
    assert.equal(explicitTrust.permission, 1);
    const explicitTrustSrc = Buffer.from(explicitTrust.content, "base64").toString("utf8");
    assert.match(explicitTrustSrc, /^function\s+[^\(\)\{\}\s]+\s*\(/);

    // Merged redirect + scriptlet (noop.js) keeps extra redirect alias without duplicate name collision
    const noop = byName.get("noop.js");
    assert.ok(noop.aliases.includes("noopjs"));
    assert.ok(noop.aliases.includes("noop-extra.js"));

    // Binary + inline data redirects
    assert.deepEqual(byName.get("1x1.gif").kind, { mime: "image/gif" });
    assert.equal(Buffer.from(byName.get("1x1.gif").content, "base64").toString("utf8"), "GIF89a");
    assert.deepEqual(byName.get("empty").kind, { mime: "text/plain" });
    assert.equal(Buffer.from(byName.get("inline.txt").content, "base64").toString("utf8"), "hello");

    // Global uniqueness across all names and aliases
    const seen = new Set();
    for (const r of resources) {
        for (const id of [r.name, ...r.aliases]) {
            assert.ok(!seen.has(id), `duplicate id ${id}`);
            seen.add(id);
        }
    }

    // Missing scriptlets.js fails cleanly
    let threw = false;
    try {
        execFileSync("node", [BUILD_SCRIPT, "--ubo-dir", tmpDir, "--out", outPath], { stdio: "pipe" });
    } catch {
        threw = true;
    }
    assert.ok(threw, "Expected missing scriptlets.js to fail");

    console.log("test-build-ubo-resources: all assertions passed");
} finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
}
