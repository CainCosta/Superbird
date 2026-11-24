const superbirdModulesContainer = document.querySelector("#superbird-modules-list");
const superbirdTreeTabsOptions = document.querySelector("#superbird-tree-tabs-options");
const superbirdTreeTabsActive = document.querySelector("#superbird-tree-tabs-active");
const superbirdTreeTabsCloseBehavior = document.querySelector("#superbird-tree-tabs-close-behavior");
const superbirdTreeTabsChildPosition = document.querySelector("#superbird-tree-tabs-child-position");
const superbirdTreeTabsAutoCollapse = document.querySelector("#superbird-tree-tabs-auto-collapse-siblings");

let AVAILABLE_MODULES = [];
let SUPERBIRD_SETTINGS = { modules: {} };
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
};

const renderModules = () => {
    superbirdModulesContainer.textContent = "";

    if (AVAILABLE_MODULES.length === 0) {
        const empty = document.createElement("p");
        empty.className = "description";
        empty.textContent = "No Superbird modules were compiled into this build.";
        superbirdModulesContainer.appendChild(empty);
        syncTreeTabsInlineOptions();
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
        });

        row.appendChild(label);
        row.appendChild(toggle);
        superbirdModulesContainer.appendChild(row);
    }

    syncTreeTabsInlineOptions();
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
            SUPERBIRD_SETTINGS = event.detail.data.superbird || { modules: {} };
        }
        if (Object.hasOwn(event.detail.data, "tabs")) {
            TAB_SETTINGS = event.detail.data.tabs || {};
        }
        renderModules();
    }
});
