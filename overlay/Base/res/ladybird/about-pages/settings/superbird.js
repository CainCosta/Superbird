/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

const superbirdModulesContainer = document.querySelector("#superbird-modules-list");
const superbirdTreeTabsOptions = document.querySelector("#superbird-tree-tabs-options");
const superbirdTreeTabsCardBody = superbirdTreeTabsOptions.querySelector(".card-body");
const superbirdTreeTabsActive = document.querySelector("#superbird-tree-tabs-active");
const superbirdTreeTabsCloseBehavior = document.querySelector("#superbird-tree-tabs-close-behavior");
const superbirdTreeTabsChildPosition = document.querySelector("#superbird-tree-tabs-child-position");
const superbirdTreeTabsAutoCollapse = document.querySelector("#superbird-tree-tabs-auto-collapse-siblings");

const superbirdPanel = document.querySelector("#tab-superbird");

let AVAILABLE_MODULES = [];
let SUPERBIRD_SETTINGS = { modules: {}, options: {}, shortcuts: {} };
let TAB_SETTINGS = {};
let VERTICAL_TABS_SUPPORTED = true;

const isModuleEnabled = moduleInfo => {
    const overrides = SUPERBIRD_SETTINGS.modules || {};
    if (Object.hasOwn(overrides, moduleInfo.id)) {
        return !!overrides[moduleInfo.id];
    }
    return !!moduleInfo.enabledByDefault;
};

const syncTreeTabsInlineOptions = () => {
    const treeModule = AVAILABLE_MODULES.find(module => module.id === "tree_tabs");
    const moduleEnabled = !!treeModule && isModuleEnabled(treeModule);

    superbirdTreeTabsOptions.classList.toggle("hidden", !moduleEnabled || !VERTICAL_TABS_SUPPORTED);
    if (!moduleEnabled || !VERTICAL_TABS_SUPPORTED) {
        return;
    }

    const active = !!TAB_SETTINGS.verticalTabsEnabled && !!TAB_SETTINGS.treeTabsEnabled;
    superbirdTreeTabsActive.checked = active;
    superbirdTreeTabsCloseBehavior.value = TAB_SETTINGS.treeTabsCloseBehavior || "promoteFirstChild";
    superbirdTreeTabsChildPosition.value = TAB_SETTINGS.treeTabsChildPosition || "bottom";
    superbirdTreeTabsAutoCollapse.checked = !!TAB_SETTINGS.treeTabsAutoCollapseSiblings;

    superbirdTreeTabsCloseBehavior.parentElement.classList.toggle("hidden", !active);
    superbirdTreeTabsChildPosition.parentElement.classList.toggle("hidden", !active);
    superbirdTreeTabsAutoCollapse.parentElement.classList.toggle("hidden", !active);
    for (const row of superbirdTreeTabsCardBody.querySelectorAll(".superbird-inline-module-option")) {
        row.classList.toggle("hidden", !active);
    }
};

/*
 * Module options and keyboard shortcuts.
 *
 * Shortcuts use Qt's portable text ("Ctrl+K", several separated by "; "). Ctrl is Command on macOS, and Meta is the
 * Control key there. Keys are picked rather than recorded, because the browser takes its own shortcuts (and every
 * Superbird one) before this page would see them.
 */

const IS_MAC = /Mac/.test(navigator.platform || navigator.userAgent);
const MODIFIERS = IS_MAC
    ? [
          { name: "Meta", label: "\u2303", title: "Control" },
          { name: "Alt", label: "\u2325", title: "Option" },
          { name: "Shift", label: "\u21E7", title: "Shift" },
          { name: "Ctrl", label: "\u2318", title: "Command" },
      ]
    : [
          { name: "Ctrl", label: "Ctrl", title: "Ctrl" },
          { name: "Alt", label: "Alt", title: "Alt" },
          { name: "Shift", label: "Shift", title: "Shift" },
          { name: "Meta", label: "Super", title: "Super" },
      ];
// The order shortcuts are written in, in portable text.
const PORTABLE_ORDER = ["Meta", "Ctrl", "Alt", "Shift"];

const KEY_GROUPS = [
    { label: "Letters", keys: "ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("") },
    { label: "Digits", keys: "1234567890".split("") },
    { label: "Function keys", keys: Array.from({ length: 12 }, (_, index) => `F${index + 1}`) },
    { label: "Punctuation", keys: [",", ".", "/", ";", "'", "[", "]", "\\", "-", "=", "`"] },
    {
        label: "Other keys",
        keys: ["Space", "Return", "Tab", "Esc", "Backspace", "Del", "Up", "Down", "Left", "Right", "Home", "End", "PgUp", "PgDown"],
    },
];
const MAC_KEY_SYMBOLS = {
    Up: "\u2191",
    Down: "\u2193",
    Left: "\u2190",
    Right: "\u2192",
    Return: "\u21A9",
    Tab: "\u21E5",
    Esc: "\u238B",
    Backspace: "\u232B",
    Del: "\u2326",
    PgUp: "\u21DE",
    PgDown: "\u21DF",
    Home: "\u2196",
    End: "\u2198",
};
const OTHER_KEY_NAMES = { Del: "Delete", PgUp: "Page Up", PgDown: "Page Down", Esc: "Escape" };

const ensureSuperbirdStyles = () => {
    if (document.querySelector("#superbird-settings-style")) {
        return;
    }
    const style = document.createElement("style");
    style.id = "superbird-settings-style";
    style.textContent = `
        .superbird-shortcut-group-title {
            margin: 18px 0 0;
            font-size: 12px;
            font-weight: 600;
            color: var(--muted);
        }
        .superbird-shortcut-keys {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            justify-content: flex-end;
            gap: 6px;
            flex-shrink: 0;
        }
        .superbird-shortcut-chip {
            display: inline-flex;
            align-items: center;
            gap: 2px;
            height: 24px;
            padding: 0 2px 0 8px;
            border-radius: 6px;
            background-color: var(--control);
            color: var(--text);
            font-size: 12px;
            font-variant-numeric: tabular-nums;
            letter-spacing: 0.04em;
            white-space: nowrap;
        }
        .superbird-shortcut-chip.conflict {
            box-shadow: inset 0 0 0 1px var(--error);
        }
        .superbird-shortcut-chip button,
        .superbird-shortcut-link {
            font: inherit;
            font-size: 12px;
            color: var(--muted);
            background: none;
            border: none;
            border-radius: 5px;
            cursor: pointer;
        }
        .superbird-shortcut-chip button {
            width: 20px;
            height: 20px;
            padding: 0;
            font-size: 14px;
            line-height: 1;
        }
        .superbird-shortcut-chip button:hover,
        .superbird-shortcut-link:hover {
            color: var(--text);
            background-color: var(--control-hover);
        }
        .superbird-shortcut-link {
            height: 24px;
            padding: 0 8px;
        }
        .superbird-shortcut-none {
            font-size: 12px;
            color: var(--muted);
        }
        .superbird-shortcut-warning {
            margin: 4px 0 0;
            font-size: 12px;
            color: var(--error);
        }
        .superbird-shortcut-editor {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            justify-content: flex-end;
            gap: 6px;
            padding-bottom: 14px;
        }
        .superbird-modifier {
            min-width: 32px;
            height: 28px;
            padding: 0 8px;
            font: inherit;
            font-size: 13px;
            color: var(--text);
            background-color: var(--control);
            border: none;
            border-radius: 7px;
            cursor: pointer;
        }
        .superbird-modifier:hover {
            background-color: var(--control-hover);
        }
        .superbird-modifier[aria-pressed="true"] {
            color: white;
            background-color: var(--primary);
        }
        .superbird-shortcut-editor .superbird-shortcut-preview {
            min-width: 64px;
            margin-right: auto;
            font-size: 13px;
            font-weight: 600;
            letter-spacing: 0.04em;
        }
        .superbird-shortcut-editor .superbird-shortcut-error {
            flex-basis: 100%;
            margin: 0;
            text-align: right;
            font-size: 12px;
            color: var(--error);
        }
    `;
    document.head.appendChild(style);
};

const parseShortcut = text => {
    const trimmed = text.trim();
    if (!trimmed) {
        return null;
    }
    // The key is what follows the last "+" that isn't the key itself.
    const parts = trimmed.split("+");
    let key = parts.pop();
    if (key === "" && parts.length > 0 && parts[parts.length - 1] === "") {
        parts.pop();
        key = "+";
    }
    const modifiers = new Set(parts.filter(part => PORTABLE_ORDER.includes(part)));
    if (!key) {
        return null;
    }
    return { modifiers, key };
};

const formatPortable = shortcut => [...PORTABLE_ORDER.filter(name => shortcut.modifiers.has(name)), shortcut.key].join("+");

const normalizeShortcut = text => {
    const parsed = parseShortcut(text);
    return parsed ? formatPortable(parsed) : "";
};

const parseShortcutList = text =>
    (text || "")
        .split("; ")
        .map(normalizeShortcut)
        .filter((shortcut, index, all) => shortcut && all.indexOf(shortcut) === index);

const formatNative = text => {
    const shortcut = parseShortcut(text);
    if (!shortcut) {
        return "";
    }
    if (IS_MAC) {
        const symbols = MODIFIERS.filter(modifier => shortcut.modifiers.has(modifier.name)).map(modifier => modifier.label);
        return symbols.join("") + (MAC_KEY_SYMBOLS[shortcut.key] || shortcut.key);
    }
    const names = MODIFIERS.filter(modifier => shortcut.modifiers.has(modifier.name)).map(modifier => modifier.label);
    return [...names, OTHER_KEY_NAMES[shortcut.key] || shortcut.key].join("+");
};

const isShortcutOverridden = command => Object.hasOwn(SUPERBIRD_SETTINGS.shortcuts || {}, command.command);

const effectiveShortcuts = command =>
    parseShortcutList(isShortcutOverridden(command) ? SUPERBIRD_SETTINGS.shortcuts[command.command] : command.default);

const moduleSettings = moduleInfo => {
    const settings = moduleInfo.settings || {};
    return {
        options: Array.isArray(settings.options) ? settings.options : [],
        shortcuts: Array.isArray(settings.shortcuts) ? settings.shortcuts : [],
    };
};

const optionValue = (moduleInfo, option) => {
    const values = (SUPERBIRD_SETTINGS.options || {})[moduleInfo.id] || {};
    return Object.hasOwn(values, option.id) ? values[option.id] : option.default;
};

const setOptionValue = (moduleInfo, option, value) => {
    SUPERBIRD_SETTINGS.options = { ...(SUPERBIRD_SETTINGS.options || {}) };
    SUPERBIRD_SETTINGS.options[moduleInfo.id] = { ...(SUPERBIRD_SETTINGS.options[moduleInfo.id] || {}), [option.id]: value };
    ladybird.sendMessage("setSuperbirdModuleOption", { module: moduleInfo.id, option: option.id, value });
};

const saveShortcuts = (command, shortcuts) => {
    SUPERBIRD_SETTINGS.shortcuts = { ...(SUPERBIRD_SETTINGS.shortcuts || {}) };
    const isDefault = shortcuts.join("; ") === parseShortcutList(command.default).join("; ");
    if (isDefault) {
        delete SUPERBIRD_SETTINGS.shortcuts[command.command];
        ladybird.sendMessage("setSuperbirdShortcut", { command: command.command });
    } else {
        SUPERBIRD_SETTINGS.shortcuts[command.command] = shortcuts.join("; ");
        ladybird.sendMessage("setSuperbirdShortcut", { command: command.command, shortcut: shortcuts.join("; ") });
    }
    renderModuleSettings();
};

const makeRow = (title, description) => {
    const row = document.createElement("div");
    row.className = "card-group inline-container control-row";
    const label = document.createElement("label");
    label.appendChild(document.createTextNode(title));
    if (description) {
        const desc = document.createElement("p");
        desc.className = "description";
        desc.textContent = description;
        label.appendChild(desc);
    }
    row.appendChild(label);
    return { row, label };
};

const renderOption = (moduleInfo, option, container) => {
    const { row, label } = makeRow(option.label, option.description);
    const inputId = `superbird-option-${moduleInfo.id}-${option.id}`;
    label.htmlFor = inputId;

    if (option.kind === "choice") {
        const select = document.createElement("select");
        select.id = inputId;
        for (const choice of option.choices || []) {
            const element = document.createElement("option");
            element.value = choice.value;
            element.textContent = choice.label;
            select.appendChild(element);
        }
        select.value = optionValue(moduleInfo, option);
        select.addEventListener("change", () => setOptionValue(moduleInfo, option, select.value));
        row.appendChild(select);
    } else {
        const toggle = document.createElement("input");
        toggle.id = inputId;
        toggle.type = "checkbox";
        toggle.setAttribute("switch", "");
        toggle.checked = optionValue(moduleInfo, option) === "true";
        toggle.addEventListener("change", () => setOptionValue(moduleInfo, option, toggle.checked ? "true" : "false"));
        row.appendChild(toggle);
    }
    container.appendChild(row);
    return row;
};

// Which commands each shortcut is on, so a shortcut given twice can be pointed out. The first command keeps it.
const shortcutOwners = modules => {
    const owners = new Map();
    for (const moduleInfo of modules) {
        for (const command of moduleSettings(moduleInfo).shortcuts) {
            for (const shortcut of effectiveShortcuts(command)) {
                if (!owners.has(shortcut)) {
                    owners.set(shortcut, []);
                }
                owners.get(shortcut).push(command);
            }
        }
    }
    return owners;
};

let OPEN_EDITOR_COMMAND = null;

const renderShortcutEditor = (command, current, container) => {
    const editor = document.createElement("div");
    editor.className = "superbird-shortcut-editor";

    // Command (Ctrl in portable text) on macOS, Ctrl elsewhere.
    const pressed = new Set(["Ctrl"]);
    const preview = document.createElement("span");
    preview.className = "superbird-shortcut-preview";

    const select = document.createElement("select");
    select.setAttribute("aria-label", "Key");
    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Key\u2026";
    select.appendChild(placeholder);
    for (const group of KEY_GROUPS) {
        const optgroup = document.createElement("optgroup");
        optgroup.label = group.label;
        for (const key of group.keys) {
            const option = document.createElement("option");
            option.value = key;
            option.textContent = IS_MAC && MAC_KEY_SYMBOLS[key] ? `${MAC_KEY_SYMBOLS[key]}  ${OTHER_KEY_NAMES[key] || key}` : OTHER_KEY_NAMES[key] || key;
            optgroup.appendChild(option);
        }
        select.appendChild(optgroup);
    }

    const error = document.createElement("p");
    error.className = "superbird-shortcut-error hidden";

    const save = document.createElement("button");
    save.className = "primary-button";
    save.textContent = "Add";

    const candidate = () => (select.value ? formatPortable({ modifiers: pressed, key: select.value }) : "");

    const problem = () => {
        const key = select.value;
        if (!key) {
            return "Pick a key.";
        }
        const isFunctionKey = /^F\d+$/.test(key);
        if (!isFunctionKey && !pressed.has("Ctrl") && !pressed.has("Meta") && !pressed.has("Alt")) {
            return IS_MAC
                ? "Add \u2318, \u2303 or \u2325, or pick a function key, so typing isn't taken over."
                : "Add Ctrl, Alt or Super, or pick a function key, so typing isn't taken over.";
        }
        if (current.includes(candidate())) {
            return "This command already has that shortcut.";
        }
        return null;
    };

    const update = () => {
        const text = candidate();
        preview.textContent = text ? formatNative(text) : "";
        const message = select.value ? problem() : null;
        error.textContent = message || "";
        error.classList.toggle("hidden", !message);
        save.disabled = !!problem();
    };

    editor.appendChild(preview);
    for (const modifier of MODIFIERS) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "superbird-modifier";
        button.textContent = modifier.label;
        button.title = modifier.title;
        button.setAttribute("aria-label", modifier.title);
        button.setAttribute("aria-pressed", pressed.has(modifier.name) ? "true" : "false");
        button.addEventListener("click", () => {
            if (pressed.has(modifier.name)) {
                pressed.delete(modifier.name);
            } else {
                pressed.add(modifier.name);
            }
            button.setAttribute("aria-pressed", pressed.has(modifier.name) ? "true" : "false");
            update();
        });
        editor.appendChild(button);
    }
    editor.appendChild(select);

    const cancel = document.createElement("button");
    cancel.className = "secondary-button";
    cancel.textContent = "Cancel";
    cancel.addEventListener("click", () => {
        OPEN_EDITOR_COMMAND = null;
        renderModuleSettings();
    });

    save.addEventListener("click", () => {
        if (problem()) {
            return;
        }
        OPEN_EDITOR_COMMAND = null;
        saveShortcuts(command, [...current, candidate()]);
    });
    select.addEventListener("change", update);

    editor.appendChild(cancel);
    editor.appendChild(save);
    editor.appendChild(error);
    container.appendChild(editor);
    update();
    select.focus();
};

const renderShortcut = (command, owners, container) => {
    const current = effectiveShortcuts(command);
    const { row, label } = makeRow(command.title);

    const keys = document.createElement("div");
    keys.className = "superbird-shortcut-keys";

    const conflicts = [];
    if (current.length === 0) {
        const none = document.createElement("span");
        none.className = "superbird-shortcut-none";
        none.textContent = "None";
        keys.appendChild(none);
    }
    for (const shortcut of current) {
        const chip = document.createElement("span");
        chip.className = "superbird-shortcut-chip";
        chip.appendChild(document.createTextNode(formatNative(shortcut)));

        const sharing = owners.get(shortcut) || [];
        if (sharing.length > 1) {
            chip.classList.add("conflict");
            const keys = formatNative(shortcut);
            if (sharing[0].command === command.command) {
                const others = sharing.slice(1).map(other => `\u201C${other.title}\u201D`).join(", ");
                conflicts.push(`${keys} is also given to ${others}, which won't get it.`);
            } else {
                conflicts.push(`${keys} does nothing here: \u201C${sharing[0].title}\u201D already has it.`);
            }
        }

        const remove = document.createElement("button");
        remove.type = "button";
        remove.textContent = "\u00D7";
        remove.title = "Remove this shortcut";
        remove.setAttribute("aria-label", `Remove ${formatNative(shortcut)}`);
        remove.addEventListener("click", () => saveShortcuts(command, current.filter(other => other !== shortcut)));
        chip.appendChild(remove);
        keys.appendChild(chip);
    }

    const add = document.createElement("button");
    add.type = "button";
    add.className = "superbird-shortcut-link";
    add.textContent = current.length === 0 ? "Add" : "Add\u2026";
    add.addEventListener("click", () => {
        OPEN_EDITOR_COMMAND = OPEN_EDITOR_COMMAND === command.command ? null : command.command;
        renderModuleSettings();
    });
    keys.appendChild(add);

    if (isShortcutOverridden(command)) {
        const reset = document.createElement("button");
        reset.type = "button";
        reset.className = "superbird-shortcut-link";
        reset.textContent = "Reset";
        reset.title = command.default ? `Back to ${parseShortcutList(command.default).map(formatNative).join(", ")}` : "Back to none";
        reset.addEventListener("click", () => saveShortcuts(command, parseShortcutList(command.default)));
        keys.appendChild(reset);
    }

    for (const conflict of conflicts) {
        const warning = document.createElement("p");
        warning.className = "superbird-shortcut-warning";
        warning.textContent = conflict;
        label.appendChild(warning);
    }

    row.appendChild(keys);
    container.appendChild(row);
    if (OPEN_EDITOR_COMMAND === command.command) {
        renderShortcutEditor(command, current, container);
    }
};

const makeSection = (title, description) => {
    const section = document.createElement("div");
    section.className = "superbird-module-settings";
    const heading = document.createElement("h3");
    heading.className = "card-title";
    heading.textContent = title;
    section.appendChild(heading);
    const card = document.createElement("div");
    card.className = "card";
    const body = document.createElement("div");
    body.className = "card-body";
    if (description) {
        const desc = document.createElement("p");
        desc.className = "description";
        desc.textContent = description;
        body.appendChild(desc);
    }
    card.appendChild(body);
    section.appendChild(card);
    return { section, body };
};

const renderModuleSettings = () => {
    ensureSuperbirdStyles();
    for (const old of superbirdPanel.querySelectorAll(".superbird-module-settings")) {
        old.remove();
    }
    for (const old of superbirdTreeTabsCardBody.querySelectorAll(".superbird-inline-module-option")) {
        old.remove();
    }

    const enabledModules = AVAILABLE_MODULES.filter(isModuleEnabled);
    const treeTabsActive = !!TAB_SETTINGS.verticalTabsEnabled && !!TAB_SETTINGS.treeTabsEnabled;

    for (const moduleInfo of enabledModules) {
        const { options } = moduleSettings(moduleInfo);
        if (options.length === 0) {
            continue;
        }
        if (moduleInfo.id === "tree_tabs") {
            for (const option of options) {
                const row = renderOption(moduleInfo, option, superbirdTreeTabsCardBody);
                row.classList.add("superbird-inline-module-option");
                row.classList.toggle("hidden", !treeTabsActive);
            }
            continue;
        }
        const { section, body } = makeSection(moduleInfo.name);
        for (const option of options) {
            renderOption(moduleInfo, option, body);
        }
        superbirdPanel.appendChild(section);
    }

    const withShortcuts = enabledModules.filter(moduleInfo => moduleSettings(moduleInfo).shortcuts.length > 0);
    if (withShortcuts.length === 0) {
        return;
    }
    const tabNumbers = IS_MAC ? "\u21E7\u23181\u20139" : "Ctrl+Shift+1\u20139";
    const plainNumbers = IS_MAC ? "\u23181\u20139" : "Ctrl+1\u20139";
    const { section, body } = makeSection(
        "Keyboard Shortcuts",
        `Shortcuts for Superbird commands, in every window. Tabs are picked by number with ${tabNumbers}, and with ${plainNumbers} too while no command here uses those.`
    );
    const owners = shortcutOwners(withShortcuts);
    for (const moduleInfo of withShortcuts) {
        const groupTitle = document.createElement("h4");
        groupTitle.className = "superbird-shortcut-group-title";
        groupTitle.textContent = moduleInfo.name;
        body.appendChild(groupTitle);
        for (const command of moduleSettings(moduleInfo).shortcuts) {
            renderShortcut(command, owners, body);
        }
    }
    superbirdPanel.appendChild(section);
};

const renderModules = () => {
    superbirdModulesContainer.textContent = "";

    if (AVAILABLE_MODULES.length === 0) {
        const empty = document.createElement("p");
        empty.className = "description";
        empty.textContent = "No Superbird modules were compiled into this build.";
        superbirdModulesContainer.appendChild(empty);
        syncTreeTabsInlineOptions();
        renderModuleSettings();
        return;
    }

    for (const moduleInfo of AVAILABLE_MODULES) {
        const row = document.createElement("div");
        row.className = "card-group inline-container control-row";

        const inputId = `superbird-module-${moduleInfo.id}`;
        const label = document.createElement("label");
        label.htmlFor = inputId;
        label.appendChild(document.createTextNode(moduleInfo.name));

        if (moduleInfo.description) {
            const desc = document.createElement("p");
            desc.className = "description";
            desc.textContent = moduleInfo.description;
            label.appendChild(desc);
        }

        const toggle = document.createElement("input");
        toggle.id = inputId;
        toggle.type = "checkbox";
        toggle.setAttribute("switch", "");
        toggle.checked = isModuleEnabled(moduleInfo);
        toggle.addEventListener("change", () => {
            SUPERBIRD_SETTINGS.modules = {
                ...(SUPERBIRD_SETTINGS.modules || {}),
                [moduleInfo.id]: toggle.checked,
            };
            ladybird.sendMessage("setSuperbirdModuleEnabled", {
                id: moduleInfo.id,
                enabled: toggle.checked,
            });
            syncTreeTabsInlineOptions();
            renderModuleSettings();
        });

        row.appendChild(label);
        row.appendChild(toggle);
        superbirdModulesContainer.appendChild(row);
    }

    syncTreeTabsInlineOptions();
    renderModuleSettings();
};

const saveTabSettings = () => {
    ladybird.sendMessage("setTabSettings", TAB_SETTINGS);
};

superbirdTreeTabsActive.addEventListener("change", () => {
    TAB_SETTINGS.treeTabsEnabled = superbirdTreeTabsActive.checked;
    if (superbirdTreeTabsActive.checked && !TAB_SETTINGS.verticalTabsEnabled) {
        TAB_SETTINGS.verticalTabsEnabled = true;
    }
    syncTreeTabsInlineOptions();
    saveTabSettings();
});

superbirdTreeTabsCloseBehavior.addEventListener("change", () => {
    TAB_SETTINGS.treeTabsCloseBehavior = superbirdTreeTabsCloseBehavior.value;
    saveTabSettings();
});

superbirdTreeTabsChildPosition.addEventListener("change", () => {
    TAB_SETTINGS.treeTabsChildPosition = superbirdTreeTabsChildPosition.value;
    saveTabSettings();
});

superbirdTreeTabsAutoCollapse.addEventListener("change", () => {
    TAB_SETTINGS.treeTabsAutoCollapseSiblings = superbirdTreeTabsAutoCollapse.checked;
    saveTabSettings();
});

document.addEventListener("WebUIMessage", event => {
    if (event.detail.name === "loadFeatures") {
        VERTICAL_TABS_SUPPORTED = !!event.detail.data?.verticalTabs;
        AVAILABLE_MODULES = Array.isArray(event.detail.data?.superbirdModules)
            ? event.detail.data.superbirdModules
            : [];
        renderModules();
    } else if (event.detail.name === "loadSettings") {
        if (Object.hasOwn(event.detail.data, "superbird")) {
            SUPERBIRD_SETTINGS = { modules: {}, options: {}, shortcuts: {}, ...(event.detail.data.superbird || {}) };
        }
        if (Object.hasOwn(event.detail.data, "tabs")) {
            TAB_SETTINGS = event.detail.data.tabs || {};
        }
        renderModules();
    }
});
