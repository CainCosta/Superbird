/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::collections::{HashMap, HashSet, VecDeque};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use adblock::cosmetic_filter_cache::ProceduralOrActionFilter;
use adblock::filters::cosmetic::CosmeticFilterOperator;
use adblock::lists::{FilterFormat, FilterSet, ParseOptions};
use adblock::resources::{
    InMemoryResourceStorage, PermissionMask, Resource, ResourceImpl, ResourceStorageBackend,
};

use crate::{ContentBlockerEngine, GenericSelectorListRules};

const MAX_IF_DEPTH: usize = 64;

#[derive(Clone, Debug)]
pub struct Env {
    vars: HashMap<String, bool>,
}

impl Default for Env {
    fn default() -> Self {
        Self::superbird_default()
    }
}

impl Env {
    pub fn superbird_default() -> Self {
        let mut vars = HashMap::new();
        vars.insert("ext_ublock".to_string(), true);
        vars.insert("cap_user_stylesheet".to_string(), true);

        for name in [
            "adguard",
            "adguard_app_android",
            "adguard_app_ios",
            "adguard_app_mac",
            "adguard_app_windows",
            "adguard_ext_android_cb",
            "adguard_ext_chromium",
            "adguard_ext_edge",
            "adguard_ext_firefox",
            "adguard_ext_opera",
            "adguard_ext_safari",
            "cap_html_filtering",
            "cap_ipaddress",
            "env_chromium",
            "env_edge",
            "env_firefox",
            "env_mobile",
            "env_mv3",
            "env_safari",
            "ext_abp",
            "ext_devbuild",
            "ext_ubol",
            "false",
        ] {
            vars.insert(name.to_string(), false);
        }

        Self { vars }
    }

    #[cfg(test)]
    pub fn with_var(mut self, name: &str, value: bool) -> Self {
        self.vars.insert(name.to_string(), value);
        self
    }

    pub fn get(&self, name: &str) -> bool {
        self.vars.get(name).copied().unwrap_or(false)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Token<'a> {
    Ident(&'a str),
    Not,
    And,
    Or,
    LParen,
    RParen,
}

fn tokenize(input: &str) -> Result<Vec<Token<'_>>, ()> {
    let bytes = input.as_bytes();
    let mut tokens = Vec::new();
    let mut i = 0;

    while i < bytes.len() {
        let b = bytes[i];
        if b.is_ascii_whitespace() {
            i += 1;
            continue;
        }
        match b {
            b'!' => {
                tokens.push(Token::Not);
                i += 1;
            }
            b'(' => {
                tokens.push(Token::LParen);
                i += 1;
            }
            b')' => {
                tokens.push(Token::RParen);
                i += 1;
            }
            b'&' if i + 1 < bytes.len() && bytes[i + 1] == b'&' => {
                tokens.push(Token::And);
                i += 2;
            }
            b'|' if i + 1 < bytes.len() && bytes[i + 1] == b'|' => {
                tokens.push(Token::Or);
                i += 2;
            }
            c if c.is_ascii_alphanumeric() || c == b'_' || c == b'-' => {
                let start = i;
                i += 1;
                while i < bytes.len() {
                    let c = bytes[i];
                    if c.is_ascii_alphanumeric() || c == b'_' || c == b'-' {
                        i += 1;
                    } else {
                        break;
                    }
                }
                tokens.push(Token::Ident(&input[start..i]));
            }
            _ => return Err(()),
        }
    }

    Ok(tokens)
}

struct ExprParser<'a> {
    tokens: &'a [Token<'a>],
    pos: usize,
    env: &'a Env,
}

impl<'a> ExprParser<'a> {
    fn peek(&self) -> Option<&Token<'a>> {
        self.tokens.get(self.pos)
    }

    fn parse_or(&mut self, depth: usize) -> Result<bool, ()> {
        let mut lhs = self.parse_and(depth)?;
        while matches!(self.peek(), Some(Token::Or)) {
            self.pos += 1;
            let rhs = self.parse_and(depth)?;
            lhs = lhs || rhs;
        }
        Ok(lhs)
    }

    fn parse_and(&mut self, depth: usize) -> Result<bool, ()> {
        let mut lhs = self.parse_unary(depth)?;
        while matches!(self.peek(), Some(Token::And)) {
            self.pos += 1;
            let rhs = self.parse_unary(depth)?;
            lhs = lhs && rhs;
        }
        Ok(lhs)
    }

    fn parse_unary(&mut self, depth: usize) -> Result<bool, ()> {
        if depth >= MAX_IF_DEPTH {
            return Err(());
        }
        if matches!(self.peek(), Some(Token::Not)) {
            self.pos += 1;
            return Ok(!self.parse_unary(depth + 1)?);
        }
        self.parse_primary(depth)
    }

    fn parse_primary(&mut self, depth: usize) -> Result<bool, ()> {
        if depth >= MAX_IF_DEPTH {
            return Err(());
        }
        match self.peek().cloned() {
            Some(Token::Ident(name)) => {
                self.pos += 1;
                Ok(self.env.get(name))
            }
            Some(Token::LParen) => {
                self.pos += 1;
                let value = self.parse_or(depth + 1)?;
                if !matches!(self.peek(), Some(Token::RParen)) {
                    return Err(());
                }
                self.pos += 1;
                Ok(value)
            }
            _ => Err(()),
        }
    }
}

pub fn evaluate_if_expression(expr: &str, env: &Env) -> bool {
    let Ok(tokens) = tokenize(expr) else {
        return false;
    };
    if tokens.is_empty() {
        return false;
    }
    let mut parser = ExprParser {
        tokens: &tokens,
        pos: 0,
        env,
    };
    match parser.parse_or(0) {
        Ok(value) if parser.pos == tokens.len() => value,
        _ => false,
    }
}

#[derive(Clone, Copy)]
struct IfFrame {
    parent_active: bool,
    branch_active: bool,
    matched_branch: bool,
    seen_else: bool,
}

fn strip_directive_prefix<'a>(trimmed: &'a str, directive: &str) -> Option<&'a str> {
    let rest = trimmed.strip_prefix(directive)?;
    if rest.is_empty() || rest.starts_with(char::is_whitespace) {
        return Some(rest.trim());
    }
    if directive == "!#if" && rest.starts_with('(') {
        return Some(rest.trim());
    }
    None
}

pub fn preprocess_if_directives_with_env(input: &str, env: &Env) -> String {
    if !input.contains("!#") {
        return input.to_string();
    }

    let mut out = String::with_capacity(input.len());
    let mut stack: Vec<IfFrame> = Vec::new();
    let mut overflow_depth: usize = 0;

    for raw_line in input.split('\n') {
        let line = raw_line.strip_suffix('\r').unwrap_or(raw_line);
        let trimmed = line.trim();

        if let Some(expr) = strip_directive_prefix(trimmed, "!#if") {
            let currently_active =
                overflow_depth == 0 && stack.last().is_none_or(|frame| frame.branch_active);
            if stack.len() >= MAX_IF_DEPTH {
                overflow_depth += 1;
                continue;
            }
            let cond = if currently_active {
                evaluate_if_expression(expr, env)
            } else {
                false
            };
            stack.push(IfFrame {
                parent_active: currently_active,
                branch_active: currently_active && cond,
                matched_branch: cond,
                seen_else: false,
            });
            continue;
        }

        if strip_directive_prefix(trimmed, "!#else").is_some() {
            if overflow_depth > 0 {
                continue;
            }
            if let Some(top) = stack.last_mut() {
                if top.seen_else {
                    top.branch_active = false;
                } else {
                    top.seen_else = true;
                    top.branch_active = top.parent_active && !top.matched_branch;
                    top.matched_branch = true;
                }
            }
            continue;
        }

        if strip_directive_prefix(trimmed, "!#endif").is_some() {
            if overflow_depth > 0 {
                overflow_depth -= 1;
            } else {
                stack.pop();
            }
            continue;
        }

        let is_active = overflow_depth == 0 && stack.last().is_none_or(|frame| frame.branch_active);
        if is_active {
            if !out.is_empty() {
                out.push('\n');
            }
            out.push_str(line);
        }
    }

    if !out.is_empty() && input.ends_with('\n') {
        out.push('\n');
    }

    out
}

pub fn preprocess_if_directives(input: &str) -> String {
    preprocess_if_directives_with_env(input, &Env::superbird_default())
}

#[derive(Debug, Clone)]
pub struct BundleList<'a> {
    #[allow(dead_code)]
    pub id: String,
    pub trusted: bool,
    pub format: FilterFormat,
    pub content: &'a str,
}

#[derive(Debug, Clone)]
pub struct ParsedBundle<'a> {
    #[allow(dead_code)]
    pub version: u32,
    pub is_ubo_mode: bool,
    pub logger_enabled: bool,
    pub lists: Vec<BundleList<'a>>,
    pub resources_json: Option<&'a str>,
}

fn parse_header_attrs(line: &str) -> HashMap<&str, &str> {
    let mut attrs = HashMap::new();
    for token in line.split_whitespace().skip(1) {
        if let Some((key, value)) = token.split_once('=') {
            attrs.insert(key, value);
        }
    }
    attrs
}

pub fn parse_bundle(input: &str) -> ParsedBundle<'_> {
    let mut version = 1u32;
    let mut is_ubo_mode = true;
    let mut logger_enabled = false;
    let mut lists = Vec::new();
    let mut resources_json = None;

    let mut pos = 0usize;
    let len = input.len();

    while pos < len {
        let line_end = match input[pos..].find('\n') {
            Some(rel_end) => pos + rel_end,
            None => len,
        };
        let raw_line = &input[pos..line_end];
        let line = raw_line.strip_suffix('\r').unwrap_or(raw_line).trim();
        let next_pos = if line_end < len { line_end + 1 } else { len };

        if line.starts_with("!superbird:meta") {
            let attrs = parse_header_attrs(line);
            if let Some(v) = attrs.get("version").and_then(|s| s.parse::<u32>().ok()) {
                version = v;
            }
            if let Some(mode) = attrs.get("mode") {
                is_ubo_mode = *mode == "ubo";
            }
            if let Some(logger) = attrs.get("logger") {
                logger_enabled = *logger == "1";
            }
            pos = next_pos;
            continue;
        }

        if line.starts_with("!superbird:end") {
            break;
        }

        if line.starts_with("!superbird:list") || line.starts_with("!superbird:resources") {
            let is_resources = line.starts_with("!superbird:resources");
            let attrs = parse_header_attrs(line);

            let (payload, after_payload) = if let Some(bytes_str) = attrs.get("bytes") {
                match bytes_str.parse::<usize>() {
                    Ok(byte_len) => {
                        let end = next_pos.saturating_add(byte_len);
                        if end <= len && input.is_char_boundary(end) {
                            let mut after = end;
                            if input[after..].starts_with("\r\n") {
                                after += 2;
                            } else if input[after..].starts_with('\n') {
                                after += 1;
                            }
                            (&input[next_pos..end], after)
                        } else {
                            // Truncated or corrupted framed section: skip safely.
                            break;
                        }
                    }
                    Err(_) => {
                        pos = next_pos;
                        continue;
                    }
                }
            } else {
                let mut scan = next_pos;
                let mut end = len;
                while scan < len {
                    let scan_end = match input[scan..].find('\n') {
                        Some(rel) => scan + rel,
                        None => len,
                    };
                    let scan_line = input[scan..scan_end].trim();
                    if scan_line.starts_with("!superbird:") {
                        end = scan;
                        break;
                    }
                    scan = if scan_end < len { scan_end + 1 } else { len };
                }
                (&input[next_pos..end], end)
            };

            if is_resources {
                resources_json = Some(payload);
            } else {
                let id = attrs.get("id").copied().unwrap_or("").to_string();
                let trusted = attrs.get("trust").is_some_and(|v| *v == "1");
                let format = match attrs.get("format").copied() {
                    Some("hosts") => FilterFormat::Hosts,
                    _ => FilterFormat::Standard,
                };
                lists.push(BundleList {
                    id,
                    trusted,
                    format,
                    content: payload,
                });
            }

            pos = after_payload;
            continue;
        }

        pos = next_pos;
    }

    ParsedBundle {
        version,
        is_ubo_mode,
        logger_enabled,
        lists,
        resources_json,
    }
}

const SNAPSHOT_MAGIC: &[u8; 8] = b"SBCE0001";
const MAX_FILTER_LOG_ENTRIES: usize = 500;

#[derive(Clone, Debug)]
pub struct FilterLogEntry {
    pub kind: &'static str,
    pub action: &'static str,
    pub url: String,
    pub source_url: String,
    pub detail: String,
}

#[derive(Clone, Debug, Default)]
pub struct EngineStats {
    pub cache_hit: bool,
    pub load_ms: f64,
    pub list_count: usize,
    pub resource_count: usize,
}

fn fnv1a64_bundle_hash(bundle: &ParsedBundle<'_>) -> u64 {
    let mut hash: u64 = 0xcbf29ce484222325;
    let mut feed = |bytes: &[u8]| {
        for &b in bytes {
            hash ^= b as u64;
            hash = hash.wrapping_mul(0x100000001b3);
        }
        hash ^= 0xff;
        hash = hash.wrapping_mul(0x100000001b3);
    };
    feed(SNAPSHOT_MAGIC);
    feed(&[bundle.is_ubo_mode as u8, bundle.logger_enabled as u8]);
    for list in &bundle.lists {
        if list.id == "site-switches" {
            continue;
        }
        feed(list.id.as_bytes());
        feed(&[
            list.trusted as u8,
            matches!(list.format, FilterFormat::Hosts) as u8,
        ]);
        feed(list.content.as_bytes());
    }
    hash
}

fn resolve_cache_dir(input_len: usize) -> Option<PathBuf> {
    if let Ok(val) = std::env::var("SUPERBIRD_CONTENT_BLOCKER_CACHE_DIR") {
        let trimmed = val.trim();
        if trimmed.is_empty() || trimmed.eq_ignore_ascii_case("off") {
            return None;
        }
        return Some(PathBuf::from(trimmed));
    }
    if input_len >= 64 * 1024 {
        return Some(std::env::temp_dir().join("superbird-cb-cache"));
    }
    None
}

fn serialize_generic_rules(rules: &GenericSelectorListRules) -> Option<Vec<u8>> {
    let always: Vec<&str> = rules.always_needed.iter().map(|s| s.as_str()).collect();
    let val = serde_json::json!({
        "a": always,
        "c": rules.by_class,
        "i": rules.by_id,
    });
    serde_json::to_vec(&val).ok()
}

fn deserialize_generic_rules(bytes: &[u8]) -> Option<GenericSelectorListRules> {
    let val: serde_json::Value = serde_json::from_slice(bytes).ok()?;
    let obj = val.as_object()?;
    let mut rules = GenericSelectorListRules::default();
    if let Some(arr) = obj.get("a").and_then(|v| v.as_array()) {
        for item in arr {
            if let Some(s) = item.as_str() {
                rules.always_needed.insert(s.to_string());
            }
        }
    }
    for (field, target) in [("c", &mut rules.by_class), ("i", &mut rules.by_id)] {
        if let Some(map) = obj.get(field).and_then(|v| v.as_object()) {
            for (k, v) in map {
                if let Some(arr) = v.as_array() {
                    let vec: Vec<String> = arr
                        .iter()
                        .filter_map(|item| item.as_str().map(|s| s.to_string()))
                        .collect();
                    target.insert(k.clone(), vec);
                }
            }
        }
    }
    Some(rules)
}

fn try_load_snapshot(
    cache_path: &PathBuf,
) -> Option<(adblock::engine::Engine, GenericSelectorListRules)> {
    let data = std::fs::read(cache_path).ok()?;
    if data.len() < 12 || &data[..8] != SNAPSHOT_MAGIC {
        return None;
    }
    let dat_len = u32::from_le_bytes([data[8], data[9], data[10], data[11]]) as usize;
    let dat_end = 12usize.checked_add(dat_len)?;
    if dat_end > data.len() {
        return None;
    }
    let mut engine = adblock::engine::Engine::default();
    engine.deserialize(&data[12..dat_end]).ok()?;
    let generic_rules = deserialize_generic_rules(&data[dat_end..])?;
    Some((engine, generic_rules))
}

fn try_save_snapshot(
    cache_dir: &PathBuf,
    cache_path: &PathBuf,
    engine: &adblock::engine::Engine,
    generic_rules: &GenericSelectorListRules,
) {
    let Some(generic_bytes) = serialize_generic_rules(generic_rules) else {
        return;
    };
    let dat = engine.serialize();
    let Ok(dat_len_u32) = u32::try_from(dat.len()) else {
        return;
    };
    if std::fs::create_dir_all(cache_dir).is_err() {
        return;
    }
    let mut payload = Vec::with_capacity(12 + dat.len() + generic_bytes.len());
    payload.extend_from_slice(SNAPSHOT_MAGIC);
    payload.extend_from_slice(&dat_len_u32.to_le_bytes());
    payload.extend_from_slice(&dat);
    payload.extend_from_slice(&generic_bytes);

    let tmp_path = cache_dir.join(format!(
        "{}.tmp.{}",
        cache_path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("engine.sbce"),
        std::process::id()
    ));
    if std::fs::write(&tmp_path, &payload).is_ok() {
        let _ = std::fs::rename(&tmp_path, cache_path);
    }
}

pub fn record_filter_log(
    engine: &ContentBlockerEngine,
    kind: &'static str,
    action: &'static str,
    url: &str,
    source_url: &str,
    detail: String,
) {
    if !engine.logger_enabled {
        return;
    }
    if let Ok(mut log) = engine.filter_log.lock() {
        if log.len() >= MAX_FILTER_LOG_ENTRIES {
            log.pop_front();
        }
        log.push_back(FilterLogEntry {
            kind,
            action,
            url: url.to_string(),
            source_url: source_url.to_string(),
            detail,
        });
    }
}

pub fn filter_log_json(engine: &ContentBlockerEngine) -> String {
    if !engine.logger_enabled {
        return "[]".to_string();
    }
    let entries: Vec<serde_json::Value> = match engine.filter_log.lock() {
        Ok(guard) => guard
            .iter()
            .map(|e| {
                serde_json::json!({
                    "kind": e.kind,
                    "action": e.action,
                    "url": e.url,
                    "sourceUrl": e.source_url,
                    "detail": e.detail,
                })
            })
            .collect(),
        Err(_) => Vec::new(),
    };
    serde_json::to_string(&entries).unwrap_or_else(|_| "[]".to_string())
}

pub fn engine_stats_json(engine: &ContentBlockerEngine) -> String {
    let missing_count = engine
        .missing_resources
        .lock()
        .map(|g| g.len())
        .unwrap_or(0);
    let log_count = engine.filter_log.lock().map(|g| g.len()).unwrap_or(0);
    serde_json::json!({
        "uboMode": engine.is_ubo_mode,
        "cacheHit": engine.stats.cache_hit,
        "loadMs": (engine.stats.load_ms * 100.0).round() / 100.0,
        "listCount": engine.stats.list_count,
        "resourceCount": engine.stats.resource_count,
        "missingResourceCount": missing_count,
        "loggerEnabled": engine.logger_enabled,
        "logEntryCount": log_count,
    })
    .to_string()
}

struct SuperbirdResourceStorage {
    inner: InMemoryResourceStorage,
    missing_resources: Arc<Mutex<HashSet<String>>>,
}

impl ResourceStorageBackend for SuperbirdResourceStorage {
    fn get_resource(&self, resource_ident: &str) -> Option<ResourceImpl> {
        let result = self.inner.get_resource(resource_ident);
        if result.is_none() {
            if let Ok(mut missing) = self.missing_resources.lock() {
                if missing.insert(resource_ident.to_string()) {
                    eprintln!(
                        "[Superbird ContentBlocker] Missing uBO-compatible resource '{}' (skipped safely; resources may be stale or missing)",
                        resource_ident
                    );
                }
            }
        }
        result
    }
}

pub fn create_engine(input: &str) -> ContentBlockerEngine {
    let start = Instant::now();
    let bundle = parse_bundle(input);
    let mut no_cosmetic_hosts = HashSet::new();
    let mut list_count = 0usize;

    for list in &bundle.lists {
        if list.id == "site-switches" {
            for raw_line in list.content.lines() {
                let line = raw_line.trim();
                if let Some(rest) = line.strip_prefix("@@||")
                    && let Some((host, opts)) = rest.split_once("^$")
                    && opts.split(',').any(|o| o.trim() == "elemhide")
                {
                    let host = host.trim().to_ascii_lowercase();
                    if !host.is_empty() {
                        no_cosmetic_hosts.insert(host);
                    }
                }
            }
            continue;
        }
        list_count += 1;
    }

    let cache_dir = resolve_cache_dir(input.len());
    let cache_path = cache_dir.as_ref().map(|dir| {
        let hash = fnv1a64_bundle_hash(&bundle);
        dir.join(format!("engine-{:016x}.sbce", hash))
    });

    let mut cache_hit = false;
    let (mut engine, generic_selector_list_rules) =
        if let Some((cached_engine, cached_generic)) =
            cache_path.as_ref().and_then(try_load_snapshot)
        {
            cache_hit = true;
            (cached_engine, cached_generic)
        } else {
            let mut filter_set = FilterSet::new(bundle.logger_enabled);
            let mut generic_selector_list_rules = GenericSelectorListRules::default();

            for list in &bundle.lists {
                if list.id == "site-switches" {
                    continue;
                }
                let permissions = if bundle.is_ubo_mode && list.trusted {
                    PermissionMask::from_bits(1)
                } else {
                    PermissionMask::default()
                };

                let preprocessed;
                let is_standard = matches!(list.format, FilterFormat::Standard);
                let text_for_set = if bundle.is_ubo_mode && is_standard {
                    preprocessed = preprocess_if_directives(list.content);
                    preprocessed.as_str()
                } else {
                    list.content
                };

                if is_standard {
                    generic_selector_list_rules.add_rules(text_for_set);
                }

                filter_set.add_filter_list(
                    text_for_set.to_string(),
                    ParseOptions {
                        format: list.format,
                        permissions,
                        ..ParseOptions::default()
                    },
                );
            }

            let compiled = adblock::engine::Engine::new_with_filter_set(filter_set);
            if let (Some(dir), Some(path)) = (cache_dir.as_ref(), cache_path.as_ref()) {
                try_save_snapshot(dir, path, &compiled, &generic_selector_list_rules);
            }
            (compiled, generic_selector_list_rules)
        };

    let missing_resources = Arc::new(Mutex::new(HashSet::new()));
    let mut resource_count = 0usize;

    if bundle.is_ubo_mode {
        let mut storage = InMemoryResourceStorage::default();
        if let Some(resources_json) = bundle.resources_json {
            let trimmed = resources_json.trim();
            if !trimmed.is_empty() {
                match serde_json::from_str::<Vec<Resource>>(trimmed) {
                    Ok(mut resources) => {
                        resource_count = resources.len();
                        for resource in &mut resources {
                            if resource.name.starts_with("trusted-")
                                && resource.permission.to_bits() == 0
                            {
                                resource.permission = PermissionMask::from_bits(1);
                            }
                        }
                        storage = InMemoryResourceStorage::from_resources(resources);
                    }
                    Err(err) => {
                        eprintln!(
                            "[Superbird ContentBlocker] Failed to parse uBO-compatible resources JSON (continuing without scriptlets/redirects): {}",
                            err
                        );
                    }
                }
            }
        }
        engine.use_resource_storage(SuperbirdResourceStorage {
            inner: storage,
            missing_resources: Arc::clone(&missing_resources),
        });
    }

    let load_ms = start.elapsed().as_secs_f64() * 1000.0;

    ContentBlockerEngine {
        engine,
        generic_selector_list_rules,
        is_ubo_mode: bundle.is_ubo_mode,
        logger_enabled: bundle.logger_enabled,
        missing_resources,
        no_cosmetic_hosts,
        filter_log: Mutex::new(VecDeque::new()),
        stats: EngineStats {
            cache_hit,
            load_ms,
            list_count,
            resource_count,
        },
    }
}

fn extract_url_host(url: &str) -> Option<&str> {
    let after_scheme = url.split_once("://").map(|(_, rest)| rest)?;
    let authority = after_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or("");
    let host_port = authority
        .rsplit_once('@')
        .map(|(_, hp)| hp)
        .unwrap_or(authority);
    let host = if host_port.starts_with('[') {
        host_port.split(']').next().map(|h| &h[1..])?
    } else {
        host_port.split(':').next()?
    };
    if host.is_empty() { None } else { Some(host) }
}

pub fn is_cosmetic_disabled_for_url(engine: &ContentBlockerEngine, url: &str) -> bool {
    if !engine.is_ubo_mode || engine.no_cosmetic_hosts.is_empty() {
        return false;
    }
    let Some(host) = extract_url_host(url) else {
        return false;
    };
    let lower = host.to_ascii_lowercase();
    if engine.no_cosmetic_hosts.contains(&lower) {
        return true;
    }
    // Also match subdomains (e.g. "m.example.com" matches disabled "example.com")
    let mut tail = lower.as_str();
    while let Some((_, rest)) = tail.split_once('.') {
        if engine.no_cosmetic_hosts.contains(rest) {
            return true;
        }
        tail = rest;
    }
    false
}

const PROCEDURAL_COSMETIC_RUNNER_JS: &str = r#"(function(rules) {
if (!Array.isArray(rules) || rules.length === 0) return;
const _qsa = Document.prototype.querySelectorAll;
const _elQsa = Element.prototype.querySelectorAll;
const _closest = Element.prototype.closest;
const _gcs = window.getComputedStyle;
const _evalXPath = Document.prototype.evaluate;
const _MO = window.MutationObserver;
const _rAF = window.requestAnimationFrame;
const _setTimeout = window.setTimeout;

function parseMatcher(raw) {
    if (typeof raw !== "string") return null;
    const m = /^\/(.+)\/([dgimsuvy]*)$/.exec(raw);
    if (m) {
        try {
            const re = new RegExp(m[1], m[2]);
            return (s) => re.test(s);
        } catch (_) {
            return () => false;
        }
    }
    return (s) => s.indexOf(raw) !== -1;
}

function parseExactOrRegex(raw) {
    if (typeof raw !== "string") return null;
    const trimmed = raw.trim();
    const m = /^\/(.+)\/([dgimsuvy]*)$/.exec(trimmed);
    if (m) {
        try {
            const re = new RegExp(m[1], m[2]);
            return (s) => re.test(s);
        } catch (_) {
            return () => false;
        }
    }
    const unquoted = (trimmed.startsWith('"') && trimmed.endsWith('"')) || (trimmed.startsWith("'") && trimmed.endsWith("'"))
        ? trimmed.slice(1, -1)
        : trimmed;
    return (s) => s === unquoted;
}

function unquote(s) {
    const t = s.trim();
    if (t.length >= 2 && ((t[0] === '"' && t[t.length - 1] === '"') || (t[0] === "'" && t[t.length - 1] === "'"))) {
        return t.slice(1, -1);
    }
    return t;
}

function matchAttr(el, arg) {
    if (!el.attributes) return false;
    let raw = arg.trim();
    let eqIdx = -1;
    let quote = null;
    let inRegex = false;
    for (let i = 0; i < raw.length; i++) {
        const ch = raw[i];
        if (ch === "\\" && i + 1 < raw.length) { i++; continue; }
        if (quote) { if (ch === quote) quote = null; continue; }
        if (inRegex) { if (ch === "/") inRegex = false; continue; }
        if (ch === '"' || ch === "'") { quote = ch; continue; }
        if (ch === "/" && i === 0) { inRegex = true; continue; }
        if (ch === "=") { eqIdx = i; break; }
    }
    const nameSpec = eqIdx === -1 ? raw : raw.slice(0, eqIdx).trim();
    const valSpec = eqIdx === -1 ? null : raw.slice(eqIdx + 1).trim();
    const matchName = parseExactOrRegex(nameSpec);
    const matchVal = valSpec === null ? null : parseExactOrRegex(valSpec);
    if (!matchName) return false;
    for (let i = 0; i < el.attributes.length; i++) {
        const attr = el.attributes[i];
        if (!matchName(attr.name)) continue;
        if (matchVal === null || matchVal(attr.value)) return true;
    }
    return false;
}

function matchCss(el, arg, pseudo) {
    const colon = arg.indexOf(":");
    if (colon === -1) return false;
    const prop = arg.slice(0, colon).trim();
    const expected = arg.slice(colon + 1).trim();
    const matcher = parseExactOrRegex(expected);
    if (!matcher) return false;
    try {
        const style = _gcs.call(window, el, pseudo);
        if (!style) return false;
        const val = style.getPropertyValue(prop).trim();
        return matcher(val);
    } catch (_) {
        return false;
    }
}

function matchPath(arg) {
    let raw = arg.trim();
    let negate = false;
    if (raw.startsWith("!")) {
        negate = true;
        raw = raw.slice(1).trim();
    }
    const matcher = parseMatcher(raw);
    if (!matcher) return false;
    const current = (window.location.pathname || "") + (window.location.search || "");
    const matched = matcher(current);
    return negate ? !matched : matched;
}

function dedupe(nodes) {
    const seen = new Set();
    const out = [];
    for (let i = 0; i < nodes.length; i++) {
        const n = nodes[i];
        if (n && n.nodeType === 1 && !seen.has(n)) {
            seen.add(n);
            out.push(n);
        }
    }
    return out;
}

function evalRule(rule) {
    let nodes = null;
    for (let i = 0; i < rule.selector.length; i++) {
        const op = rule.selector[i];
        const arg = op.arg || "";
        switch (op.type) {
            case "css-selector": {
                if (nodes === null) {
                    try { nodes = Array.from(_qsa.call(document, arg)); } catch (_) { return []; }
                } else {
                    const next = [];
                    const scoped = /^\s*[>+~]/.test(arg) ? ":scope " + arg : arg;
                    for (let j = 0; j < nodes.length; j++) {
                        try {
                            const found = _elQsa.call(nodes[j], scoped);
                            for (let k = 0; k < found.length; k++) next.push(found[k]);
                        } catch (_) {}
                    }
                    nodes = dedupe(next);
                }
                break;
            }
            case "has-text": {
                if (nodes === null) {
                    try { nodes = Array.from(_qsa.call(document, "*")); } catch (_) { return []; }
                }
                const matcher = parseMatcher(arg);
                if (!matcher) return [];
                nodes = nodes.filter((el) => matcher(el.textContent || ""));
                break;
            }
            case "min-text-length": {
                if (nodes === null) {
                    try { nodes = Array.from(_qsa.call(document, "*")); } catch (_) { return []; }
                }
                const minLen = parseInt(arg, 10);
                if (!Number.isFinite(minLen)) return [];
                nodes = nodes.filter((el) => (el.textContent || "").trim().length >= minLen);
                break;
            }
            case "matches-path": {
                if (!matchPath(arg)) return [];
                if (nodes === null) {
                    nodes = document.documentElement ? [document.documentElement] : [];
                }
                break;
            }
            case "matches-attr": {
                if (nodes === null) {
                    try { nodes = Array.from(_qsa.call(document, "*")); } catch (_) { return []; }
                }
                nodes = nodes.filter((el) => matchAttr(el, arg));
                break;
            }
            case "matches-css":
            case "matches-css-before":
            case "matches-css-after": {
                if (nodes === null) {
                    try { nodes = Array.from(_qsa.call(document, "*")); } catch (_) { return []; }
                }
                const pseudo = op.type === "matches-css-before" ? "::before" : (op.type === "matches-css-after" ? "::after" : null);
                nodes = nodes.filter((el) => matchCss(el, arg, pseudo));
                break;
            }
            case "upward": {
                if (nodes === null) return [];
                const trimmed = arg.trim();
                const next = [];
                if (/^\d+$/.test(trimmed)) {
                    const steps = parseInt(trimmed, 10);
                    for (let j = 0; j < nodes.length; j++) {
                        let cur = nodes[j];
                        for (let s = 0; s < steps && cur; s++) cur = cur.parentElement;
                        if (cur) next.push(cur);
                    }
                } else {
                    for (let j = 0; j < nodes.length; j++) {
                        try {
                            const p = nodes[j].parentElement;
                            const anc = p ? _closest.call(p, trimmed) : null;
                            if (anc) next.push(anc);
                        } catch (_) {}
                    }
                }
                nodes = dedupe(next);
                break;
            }
            case "xpath": {
                const contexts = nodes === null ? [document] : nodes;
                const next = [];
                for (let j = 0; j < contexts.length; j++) {
                    try {
                        const snap = _evalXPath.call(document, arg, contexts[j], null, 7, null);
                        for (let k = 0; k < snap.snapshotLength; k++) {
                            const item = snap.snapshotItem(k);
                            if (item && item.nodeType === 1) next.push(item);
                        }
                    } catch (_) {}
                }
                nodes = dedupe(next);
                break;
            }
            default:
                return [];
        }
        if (!nodes || nodes.length === 0) return [];
    }
    return nodes || [];
}

function applyAction(el, action) {
    if (!action) {
        if (el.style.getPropertyValue("display") !== "none" || el.style.getPropertyPriority("display") !== "important") {
            el.style.setProperty("display", "none", "important");
        }
        return;
    }
    switch (action.type) {
        case "remove":
            el.remove();
            break;
        case "remove-attr":
            if (action.arg && el.hasAttribute(action.arg)) el.removeAttribute(action.arg);
            break;
        case "remove-class":
            if (action.arg && el.classList && el.classList.contains(action.arg)) el.classList.remove(action.arg);
            break;
        case "style": {
            const decls = String(action.arg || "").split(";");
            for (let i = 0; i < decls.length; i++) {
                const decl = decls[i].trim();
                if (!decl) continue;
                const colon = decl.indexOf(":");
                if (colon === -1) continue;
                const prop = decl.slice(0, colon).trim();
                let val = decl.slice(colon + 1).trim();
                let prio = "";
                if (/\s*!important$/i.test(val)) {
                    val = val.replace(/\s*!important$/i, "").trim();
                    prio = "important";
                }
                if (el.style.getPropertyValue(prop) !== val || el.style.getPropertyPriority(prop) !== prio) {
                    el.style.setProperty(prop, val, prio);
                }
            }
            break;
        }
    }
}

let observer = null;
let scheduled = false;

function runPass() {
    scheduled = false;
    if (!document.documentElement) return;
    if (observer) observer.disconnect();
    try {
        for (let i = 0; i < rules.length; i++) {
            const matched = evalRule(rules[i]);
            for (let j = 0; j < matched.length; j++) {
                try { applyAction(matched[j], rules[i].action || null); } catch (_) {}
            }
        }
    } finally {
        if (observer && document.documentElement) {
            observer.observe(document.documentElement, { childList: true, subtree: true, attributes: true });
        }
    }
}

function schedulePass() {
    if (scheduled) return;
    scheduled = true;
    if (typeof _rAF === "function") {
        _rAF.call(window, runPass);
    } else {
        _setTimeout.call(window, runPass, 16);
    }
}

function init() {
    if (!document.documentElement) return;
    if (!observer && typeof _MO === "function") {
        observer = new _MO(schedulePass);
    }
    runPass();
}

if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init, { once: true });
    if (document.documentElement) schedulePass();
} else {
    init();
}
window.addEventListener("popstate", schedulePass);
window.addEventListener("hashchange", schedulePass);
})("#;

const PROCEDURAL_PSEUDO_NAMES: &[(&str, &str)] = &[
    (":has-text(", "has-text"),
    (":-abp-contains(", "has-text"),
    (":matches-attr(", "matches-attr"),
    (":matches-css-before(", "matches-css-before"),
    (":matches-css-after(", "matches-css-after"),
    (":matches-css(", "matches-css"),
    (":matches-path(", "matches-path"),
    (":min-text-length(", "min-text-length"),
    (":upward(", "upward"),
    (":xpath(", "xpath"),
];

pub fn contains_procedural_operator(selector: &str) -> bool {
    PROCEDURAL_PSEUDO_NAMES
        .iter()
        .any(|(needle, _)| selector.contains(needle))
}

fn find_matching_paren(s: &str, open_paren_index: usize) -> Option<usize> {
    let bytes = s.as_bytes();
    let mut depth = 0usize;
    let mut quote: Option<u8> = None;
    let mut in_regex = false;
    let mut i = open_paren_index;
    while i < bytes.len() {
        let b = bytes[i];
        if b == b'\\' && i + 1 < bytes.len() {
            i += 2;
            continue;
        }
        if let Some(q) = quote {
            if b == q {
                quote = None;
            }
            i += 1;
            continue;
        }
        if in_regex {
            if b == b'/' {
                in_regex = false;
            }
            i += 1;
            continue;
        }
        match b {
            b'"' | b'\'' => quote = Some(b),
            b'/' if i == open_paren_index + 1 => in_regex = true,
            b'(' => depth += 1,
            b')' => {
                depth = depth.saturating_sub(1);
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn make_procedural_operator(kind: &str, arg: String) -> Option<CosmeticFilterOperator> {
    match kind {
        "has-text" => Some(CosmeticFilterOperator::HasText(arg)),
        "matches-attr" => Some(CosmeticFilterOperator::MatchesAttr(arg)),
        "matches-css" => Some(CosmeticFilterOperator::MatchesCss(arg)),
        "matches-css-before" => Some(CosmeticFilterOperator::MatchesCssBefore(arg)),
        "matches-css-after" => Some(CosmeticFilterOperator::MatchesCssAfter(arg)),
        "matches-path" => Some(CosmeticFilterOperator::MatchesPath(arg)),
        "min-text-length" => Some(CosmeticFilterOperator::MinTextLength(arg)),
        "upward" => Some(CosmeticFilterOperator::Upward(arg)),
        "xpath" => Some(CosmeticFilterOperator::Xpath(arg)),
        _ => None,
    }
}

pub fn parse_procedural_selector(selector: &str) -> Option<Vec<CosmeticFilterOperator>> {
    if !contains_procedural_operator(selector) {
        return None;
    }

    let mut ops = Vec::new();
    let bytes = selector.as_bytes();
    let mut css_start = 0usize;
    let mut i = 0usize;
    let mut quote: Option<u8> = None;
    let mut bracket_depth = 0usize;
    let mut paren_depth = 0usize;

    while i < bytes.len() {
        let b = bytes[i];
        if b == b'\\' && i + 1 < bytes.len() {
            i += 2;
            continue;
        }
        if let Some(q) = quote {
            if b == q {
                quote = None;
            }
            i += 1;
            continue;
        }
        match b {
            b'"' | b'\'' => {
                quote = Some(b);
                i += 1;
                continue;
            }
            b'[' => {
                bracket_depth += 1;
                i += 1;
                continue;
            }
            b']' => {
                bracket_depth = bracket_depth.saturating_sub(1);
                i += 1;
                continue;
            }
            b'(' => {
                paren_depth += 1;
                i += 1;
                continue;
            }
            b')' => {
                paren_depth = paren_depth.saturating_sub(1);
                i += 1;
                continue;
            }
            b':' if bracket_depth == 0 && paren_depth == 0 => {
                let tail = &selector[i..];
                if let Some((prefix, op_kind)) = PROCEDURAL_PSEUDO_NAMES
                    .iter()
                    .find(|(prefix, _)| tail.starts_with(prefix))
                {
                    let open_idx = i + prefix.len() - 1;
                    let close_idx = find_matching_paren(selector, open_idx)?;
                    let css_chunk = selector[css_start..i].trim();
                    if !css_chunk.is_empty() {
                        ops.push(CosmeticFilterOperator::CssSelector(css_chunk.to_string()));
                    }
                    let arg = selector[open_idx + 1..close_idx].to_string();
                    ops.push(make_procedural_operator(op_kind, arg)?);
                    i = close_idx + 1;
                    css_start = i;
                    continue;
                }
            }
            _ => {}
        }
        i += 1;
    }

    let trailing = selector[css_start..].trim();
    if !trailing.is_empty() {
        ops.push(CosmeticFilterOperator::CssSelector(trailing.to_string()));
    }

    if ops.iter().any(|op| !matches!(op, CosmeticFilterOperator::CssSelector(_))) {
        Some(ops)
    } else {
        None
    }
}

fn expand_filter_operators(filter: &ProceduralOrActionFilter) -> ProceduralOrActionFilter {
    let mut expanded = Vec::with_capacity(filter.selector.len());
    for op in &filter.selector {
        if let CosmeticFilterOperator::CssSelector(css) = op
            && let Some(parsed) = parse_procedural_selector(css)
        {
            expanded.extend(parsed);
        } else {
            expanded.push(op.clone());
        }
    }
    ProceduralOrActionFilter {
        selector: expanded,
        action: filter.action.clone(),
    }
}

pub fn collect_procedural_filters(
    cosmetic: &adblock::cosmetic_filter_cache::UrlSpecificResources,
) -> Vec<serde_json::Value> {
    let mut entries: Vec<(String, serde_json::Value)> = Vec::new();

    for selector in &cosmetic.hide_selectors {
        if let Some(ops) = parse_procedural_selector(selector) {
            let filter = ProceduralOrActionFilter {
                selector: ops,
                action: None,
            };
            if let Ok(value) = serde_json::to_value(&filter) {
                entries.push((selector.clone(), value));
            }
        }
    }

    for raw in &cosmetic.procedural_actions {
        let Ok(filter) = serde_json::from_str::<ProceduralOrActionFilter>(raw) else {
            continue;
        };
        let expanded = expand_filter_operators(&filter);
        if expanded.as_css().is_some() {
            continue;
        }
        if let Ok(value) = serde_json::to_value(&expanded) {
            entries.push((raw.clone(), value));
        }
    }

    entries.sort_by(|a, b| a.0.cmp(&b.0));
    entries.dedup_by(|a, b| a.0 == b.0);
    entries.into_iter().map(|(_, v)| v).collect()
}

#[cfg(test)]
pub fn procedural_filters_for_url(engine: &ContentBlockerEngine, url: &str) -> Vec<serde_json::Value> {
    if !engine.is_ubo_mode {
        return Vec::new();
    }
    let cosmetic = engine.engine.url_cosmetic_resources(url);
    collect_procedural_filters(&cosmetic)
}

pub fn injected_script_for_url(engine: &ContentBlockerEngine, url: &str) -> String {
    if !engine.is_ubo_mode {
        return String::new();
    }
    let cosmetic = engine.engine.url_cosmetic_resources(url);
    let procedural_values = if is_cosmetic_disabled_for_url(engine, url) {
        Vec::new()
    } else {
        collect_procedural_filters(&cosmetic)
    };

    let has_scriptlets = !cosmetic.injected_script.trim().is_empty();
    let has_procedural = !procedural_values.is_empty();
    if !has_scriptlets && !has_procedural {
        return String::new();
    }

    let mut out = String::new();
    if has_scriptlets {
        out.push_str("(function() {\nconst scriptletGlobals = {};\ntry {\n");
        out.push_str(&cosmetic.injected_script);
        if !out.ends_with('\n') {
            out.push('\n');
        }
        out.push_str("} catch (_superbird_scriptlet_err) {}\n})();\n");
    }

    if has_procedural {
        if let Ok(json) = serde_json::to_string(&procedural_values) {
            out.push_str(PROCEDURAL_COSMETIC_RUNNER_JS);
            out.push_str(&json);
            out.push_str(");\n");
        }
    }

    if has_scriptlets {
        record_filter_log(engine, "scriptlet", "inject", url, url, "injected".to_string());
    }
    if has_procedural {
        record_filter_log(
            engine,
            "procedural",
            "inject",
            url,
            url,
            format!("{} rules", procedural_values.len()),
        );
    }

    out
}

pub fn missing_resources_json(engine: &ContentBlockerEngine) -> String {
    let mut names: Vec<String> = match engine.missing_resources.lock() {
        Ok(guard) => guard.iter().cloned().collect(),
        Err(_) => Vec::new(),
    };
    names.sort();
    serde_json::to_string(&names).unwrap_or_else(|_| "[]".to_string())
}

pub struct NetworkCheckOutcome {
    pub matched: bool,
    pub important: bool,
    pub redirect_data_url: String,
    pub rewritten_url: String,
}

pub fn check_network_request(
    engine: &ContentBlockerEngine,
    url: &str,
    source_url: &str,
    request_type: &str,
    method: &str,
) -> NetworkCheckOutcome {
    let Ok(request) = adblock::request::Request::new(url, source_url, request_type, method) else {
        return NetworkCheckOutcome {
            matched: false,
            important: false,
            redirect_data_url: String::new(),
            rewritten_url: String::new(),
        };
    };

    let result = engine.engine.check_network_request(&request);
    let matched = result.should_block();
    if !engine.is_ubo_mode {
        return NetworkCheckOutcome {
            matched,
            important: result.important,
            redirect_data_url: String::new(),
            rewritten_url: String::new(),
        };
    }

    let redirect_data_url = result.redirect.unwrap_or_default();
    let rewritten_url = result.rewritten_url.unwrap_or_default();
    if engine.logger_enabled {
        let filter_text = result
            .filter
            .as_ref()
            .and_then(|f| f.raw_line.clone())
            .unwrap_or_default();
        if !redirect_data_url.is_empty() {
            record_filter_log(engine, "network", "redirect", url, source_url, filter_text);
        } else if matched {
            record_filter_log(engine, "network", "block", url, source_url, filter_text);
        } else if !rewritten_url.is_empty() {
            record_filter_log(
                engine,
                "network",
                "removeparam",
                url,
                source_url,
                rewritten_url.clone(),
            );
        }
    }

    NetworkCheckOutcome {
        matched,
        important: result.important,
        redirect_data_url,
        rewritten_url,
    }
}

pub fn csp_directives_for_url(
    engine: &ContentBlockerEngine,
    url: &str,
    source_url: &str,
    request_type: &str,
) -> String {
    if !engine.is_ubo_mode {
        return String::new();
    }
    let Ok(request) = adblock::request::Request::new(url, source_url, request_type, "GET") else {
        return String::new();
    };
    let directives = engine
        .engine
        .get_csp_directives(&request)
        .unwrap_or_default();
    if engine.logger_enabled && !directives.is_empty() {
        record_filter_log(engine, "csp", "inject", url, source_url, directives.clone());
    }
    directives
}

#[cfg(test)]
mod tests {
    use super::*;
    use adblock::request::Request;

    fn base64_encode(input: &[u8]) -> String {
        const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let mut out = String::with_capacity(input.len().div_ceil(3) * 4);
        for chunk in input.chunks(3) {
            let b0 = chunk[0] as u32;
            let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
            let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
            let triple = (b0 << 16) | (b1 << 8) | b2;
            out.push(TABLE[((triple >> 18) & 0x3F) as usize] as char);
            out.push(TABLE[((triple >> 12) & 0x3F) as usize] as char);
            out.push(if chunk.len() > 1 {
                TABLE[((triple >> 6) & 0x3F) as usize] as char
            } else {
                '='
            });
            out.push(if chunk.len() > 2 {
                TABLE[(triple & 0x3F) as usize] as char
            } else {
                '='
            });
        }
        out
    }

    fn make_bundle_list(id: &str, trust: u8, format: &str, body: &str) -> String {
        format!(
            "!superbird:list id={} trust={} format={} bytes={}\n{}\n",
            id,
            trust,
            format,
            body.len(),
            body
        )
    }

    fn make_bundle_resources(json: &str) -> String {
        format!("!superbird:resources bytes={}\n{}\n", json.len(), json)
    }

    #[test]
    fn if_directives_evaluate_expressions_and_branches() {
        let input = "\
! keep-before
!#if ext_ublock
kept_ublock_1
!#else
dropped_else_1
!#endif
!#if env_firefox
dropped_firefox
!#if ext_ublock
dropped_nested_under_false
!#endif
!#else
kept_else_firefox
!#if !env_safari && (ext_ublock || env_chromium)
kept_nested_expr
!#endif
!#endif
!#if unknown_flag_xyz
dropped_unknown
!#endif
! keep-after
";
        let out = preprocess_if_directives(input);
        assert!(out.contains("kept_ublock_1"));
        assert!(out.contains("kept_else_firefox"));
        assert!(out.contains("kept_nested_expr"));
        assert!(!out.contains("dropped_else_1"));
        assert!(!out.contains("dropped_firefox"));
        assert!(!out.contains("dropped_nested_under_false"));
        assert!(!out.contains("dropped_unknown"));
        assert!(out.contains("! keep-before"));
        assert!(out.contains("! keep-after"));
    }

    #[test]
    fn if_directives_handle_operator_precedence_and_unbalanced_adversarial_inputs() {
        let env = Env::default().with_var("a", true).with_var("b", false).with_var("c", true);
        // ! binds tighter than &&, which binds tighter than ||
        assert!(evaluate_if_expression("a || b && !c", &env));
        assert!(!evaluate_if_expression("!a && c", &env));
        assert!(evaluate_if_expression("!(a && b) && c", &env));
        assert!(!evaluate_if_expression("", &env));
        assert!(!evaluate_if_expression("a &&", &env));
        assert!(!evaluate_if_expression("(a || b", &env));
        assert!(!evaluate_if_expression("a $ b", &env));

        // Stray !#else and !#endif at top level and duplicate !#else must not panic or corrupt
        let adversarial = "\
before
!#else
still_emitted
!#endif
still_emitted_2
!#if env_firefox
hidden_1
!#else
shown_else
!#else
hidden_duplicate_else
!#endif
!#if ext_ublock
unclosed_true
";
        let out = preprocess_if_directives(adversarial);
        assert!(out.contains("still_emitted\n"));
        assert!(out.contains("still_emitted_2\n"));
        assert!(!out.contains("hidden_1"));
        assert!(out.contains("shown_else"));
        assert!(!out.contains("hidden_duplicate_else"));
        assert!(out.contains("unclosed_true"));

        // Deep nesting beyond MAX_IF_DEPTH stays safe
        let mut deep = String::new();
        for _ in 0..80 {
            deep.push_str("!#if ext_ublock\n");
        }
        deep.push_str("too_deep_line\n");
        for _ in 0..80 {
            deep.push_str("!#endif\n");
        }
        deep.push_str("after_deep\n");
        let deep_out = preprocess_if_directives(&deep);
        assert!(!deep_out.contains("too_deep_line"));
        assert!(deep_out.contains("after_deep"));
    }

    #[test]
    fn bundle_byte_framing_blocks_trust_escalation_smuggling() {
        let benign_script = base64_encode(b"function benign() { window.__benign = 1; }");
        let trusted_script = base64_encode(b"function trustedCookie() { document.cookie = 'pwn=1'; }");
        let resources_json = serde_json::to_string(&vec![
            serde_json::json!({
                "name": "benign.js",
                "aliases": [],
                "kind": { "mime": "application/javascript" },
                "content": benign_script,
                "dependencies": [],
                "permission": 0
            }),
            serde_json::json!({
                "name": "trusted-set-cookie.js",
                "aliases": [],
                "kind": { "mime": "application/javascript" },
                "content": trusted_script,
                "dependencies": [],
                "permission": 1
            }),
        ])
        .unwrap();

        let smuggled_untrusted = "\
example.com##+js(benign)
!superbird:list id=smuggled trust=1 format=standard
example.com##+js(trusted-set-cookie)
";
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list("custom", 0, "standard", smuggled_untrusted));
        bundle.push_str(&make_bundle_resources(&resources_json));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        assert!(engine.is_ubo_mode);

        let cosmetic = engine
            .engine
            .url_cosmetic_resources("https://example.com/page");
        assert!(
            cosmetic.injected_script.contains("window.__benign = 1"),
            "Expected benign scriptlet to run, got: {}",
            cosmetic.injected_script
        );
        assert!(
            !cosmetic.injected_script.contains("document.cookie = 'pwn=1'"),
            "Smuggled trusted scriptlet MUST NOT run from trust=0 list! Got: {}",
            cosmetic.injected_script
        );
    }

    #[test]
    fn trusted_list_can_inject_trusted_scriptlet_when_permission_granted() {
        let trusted_script = base64_encode(b"function trustedSet() { window.__trusted = 1; }");
        // Omit permission field intentionally to verify trusted- prefix defense-in-depth
        let resources_json = serde_json::to_string(&vec![serde_json::json!({
            "name": "trusted-set.js",
            "aliases": [],
            "kind": { "mime": "application/javascript" },
            "content": trusted_script
        })])
        .unwrap();

        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "example.com##+js(trusted-set)",
        ));
        bundle.push_str(&make_bundle_resources(&resources_json));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        let cosmetic = engine
            .engine
            .url_cosmetic_resources("https://example.com/");
        assert!(cosmetic.injected_script.contains("window.__trusted = 1"));
    }

    #[test]
    fn hosts_format_and_malformed_bundle_inputs_behave_safely() {
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list(
            "hostsList",
            0,
            "hosts",
            "0.0.0.0 tracker.example.com\n127.0.0.1 localhost\n",
        ));
        bundle.push_str(&make_bundle_resources("{not valid json"));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        let blocked_req = Request::new(
            "https://tracker.example.com/pixel.js",
            "https://site.test/",
            "script",
            "GET",
        )
        .unwrap();
        assert!(engine.engine.check_network_request(&blocked_req).filter.is_some());

        let localhost_req =
            Request::new("https://localhost/api", "https://site.test/", "xhr", "GET").unwrap();
        assert!(engine.engine.check_network_request(&localhost_req).filter.is_none());

        // Truncated bytes=999999999 does not panic
        let truncated = create_engine(
            "!superbird:meta version=1 mode=ubo\n!superbird:list id=bad trust=1 format=standard bytes=999999999\nabc",
        );
        assert!(truncated.is_ubo_mode);
    }

    #[test]
    fn injected_script_wraps_scriptlet_globals_and_skips_in_default_mode() {
        let helper = base64_encode(b"function safeSelf() { return scriptletGlobals; }");
        let scriptlet = base64_encode(
            b"function setVal(name, val) { const g = safeSelf(); g.ran = true; window[name] = val; }",
        );
        let resources_json = serde_json::to_string(&vec![
            serde_json::json!({
                "name": "safe-self.fn",
                "aliases": [],
                "kind": { "mime": "fn/javascript" },
                "content": helper,
                "dependencies": [],
                "permission": 0
            }),
            serde_json::json!({
                "name": "set-val.js",
                "aliases": ["sv.js"],
                "kind": { "mime": "application/javascript" },
                "content": scriptlet,
                "dependencies": ["safe-self.fn"],
                "permission": 0
            }),
        ])
        .unwrap();

        let mut ubo_bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        ubo_bundle.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "example.com##+js(sv, __sb_flag, ok)\n",
        ));
        ubo_bundle.push_str(&make_bundle_resources(&resources_json));
        ubo_bundle.push_str("!superbird:end\n");

        let engine = create_engine(&ubo_bundle);
        let script = injected_script_for_url(&engine, "https://example.com/watch");
        assert!(script.contains("const scriptletGlobals = {};"));
        assert!(script.contains("function safeSelf()"));
        assert!(script.contains("setVal(\"__sb_flag\", \"ok\")"));
        assert_eq!(missing_resources_json(&engine), "[]");

        // Exception rule suppresses injection
        let mut excepted = String::from("!superbird:meta version=1 mode=ubo\n");
        excepted.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "example.com##+js(sv, __sb_flag, ok)\nexample.com#@#+js(sv, __sb_flag, ok)\n",
        ));
        excepted.push_str(&make_bundle_resources(&resources_json));
        excepted.push_str("!superbird:end\n");
        let excepted_engine = create_engine(&excepted);
        assert!(injected_script_for_url(&excepted_engine, "https://example.com/watch").is_empty());

        // Default mode does not inject scriptlets
        let mut default_bundle = String::from("!superbird:meta version=1 mode=default\n");
        default_bundle.push_str(&make_bundle_list(
            "easyList",
            0,
            "standard",
            "example.com##+js(sv, __sb_flag, ok)\n",
        ));
        default_bundle.push_str(&make_bundle_resources(&resources_json));
        default_bundle.push_str("!superbird:end\n");
        let default_engine = create_engine(&default_bundle);
        assert!(!default_engine.is_ubo_mode);
        assert!(injected_script_for_url(&default_engine, "https://example.com/watch").is_empty());
    }

    #[test]
    fn missing_scriptlet_fails_safely_and_records_missing_resource() {
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "example.com##+js(future-brand-new-scriptlet, foo)\n",
        ));
        bundle.push_str(&make_bundle_resources("[]"));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        let script = injected_script_for_url(&engine, "https://example.com/");
        assert!(script.is_empty());
        assert_eq!(
            missing_resources_json(&engine),
            "[\"future-brand-new-scriptlet.js\"]"
        );
    }

    #[test]
    fn redirects_removeparam_csp_and_websocket_matching() {
        let noop_js = base64_encode(b"window.__redirectedNoop = true;");
        let resources_json = serde_json::to_string(&vec![serde_json::json!({
            "name": "noop.js",
            "aliases": ["noopjs"],
            "kind": { "mime": "application/javascript" },
            "content": noop_js,
            "dependencies": [],
            "permission": 0
        })])
        .unwrap();

        let rules = "\
||ads.example/script.js$script,redirect=noopjs
||site.example^$removeparam=utm_source
||site.example^$removeparam=fbclid
||csp.example^$csp=script-src 'self'
||ws.example^$websocket
";
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list("uboFilters", 1, "standard", rules));
        bundle.push_str(&make_bundle_resources(&resources_json));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);

        // Redirect
        let redir = check_network_request(
            &engine,
            "https://ads.example/script.js",
            "https://site.example/",
            "script",
            "GET",
        );
        assert!(redir.matched);
        assert!(
            redir
                .redirect_data_url
                .starts_with("data:application/javascript;base64,"),
            "Unexpected redirect: {}",
            redir.redirect_data_url
        );

        // $removeparam
        let stripped = check_network_request(
            &engine,
            "https://site.example/page?id=42&utm_source=ads&fbclid=xyz",
            "https://site.example/",
            "document",
            "GET",
        );
        assert!(!stripped.matched);
        assert_eq!(stripped.rewritten_url, "https://site.example/page?id=42");

        // $csp
        let csp = csp_directives_for_url(
            &engine,
            "https://csp.example/index.html",
            "https://csp.example/index.html",
            "document",
        );
        assert_eq!(csp, "script-src 'self'");

        // $websocket (ws:// and wss://)
        let ws_block = check_network_request(
            &engine,
            "wss://ws.example/live",
            "https://site.example/",
            "websocket",
            "GET",
        );
        assert!(ws_block.matched);
        let ws_plain = check_network_request(
            &engine,
            "ws://ws.example/live",
            "https://site.example/",
            "websocket",
            "GET",
        );
        assert!(ws_plain.matched);
        let ws_other = check_network_request(
            &engine,
            "wss://allowed.example/live",
            "https://site.example/",
            "websocket",
            "GET",
        );
        assert!(!ws_other.matched);

        // Default mode disables redirects, removeparam, and CSP injection
        let mut default_bundle = String::from("!superbird:meta version=1 mode=default\n");
        default_bundle.push_str(&make_bundle_list("easyList", 0, "standard", rules));
        default_bundle.push_str(&make_bundle_resources(&resources_json));
        default_bundle.push_str("!superbird:end\n");
        let default_engine = create_engine(&default_bundle);

        let default_redir = check_network_request(
            &default_engine,
            "https://ads.example/script.js",
            "https://site.example/",
            "script",
            "GET",
        );
        assert!(default_redir.redirect_data_url.is_empty());
        let default_strip = check_network_request(
            &default_engine,
            "https://site.example/page?id=42&utm_source=ads",
            "https://site.example/",
            "document",
            "GET",
        );
        assert!(default_strip.rewritten_url.is_empty());
        assert!(
            csp_directives_for_url(
                &default_engine,
                "https://csp.example/",
                "https://csp.example/",
                "document"
            )
            .is_empty()
        );
    }

    #[test]
    fn procedural_and_action_cosmetic_rules_emit_css_or_runner() {
        let rules = "\
example.com##.styled:style(opacity: 0.5 !important)
example.com##.card:has-text(Sponsored):upward(1)
example.com##.removable:remove()
example.com##.attr-target:remove-attr(data-ad)
example.com##.class-target:remove-class(ad-highlight)
example.com##.css-target:matches-css(z-index: 999)
example.com##:xpath(//div[@data-promo='1'])
";
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list("uboFilters", 1, "standard", rules));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        let procedural = procedural_filters_for_url(&engine, "https://example.com/");
        // Pure :style() is handled via CSS, not procedural runner; the remaining 6 are procedural/action rules
        assert_eq!(procedural.len(), 6);

        let script = injected_script_for_url(&engine, "https://example.com/");
        assert!(script.contains("has-text"));
        assert!(script.contains("upward"));
        assert!(script.contains("remove-attr"));
        assert!(script.contains("remove-class"));
        assert!(script.contains("matches-css"));
        assert!(script.contains("xpath"));
        assert!(!script.contains("opacity: 0.5"));
    }

    #[test]
    fn site_switch_elemhide_disables_cosmetic_rules_for_host() {
        let mut bundle = String::from("!superbird:meta version=1 mode=ubo\n");
        bundle.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "##.generic-ad\nexample.com##.site-ad\nexample.com##.card:has-text(Ad)\n",
        ));
        bundle.push_str(&make_bundle_list(
            "site-switches",
            0,
            "standard",
            "@@||example.com^$elemhide,generichide\n",
        ));
        bundle.push_str("!superbird:end\n");

        let engine = create_engine(&bundle);
        assert!(is_cosmetic_disabled_for_url(&engine, "https://example.com/"));
        assert!(is_cosmetic_disabled_for_url(&engine, "https://sub.example.com/page"));
        assert!(!is_cosmetic_disabled_for_url(&engine, "https://other.test/"));
        assert!(crate::cosmetic_css_for_url(&engine, "https://example.com/", &["generic-ad"], &[]).is_empty());
        assert!(injected_script_for_url(&engine, "https://example.com/").is_empty());
        assert!(crate::cosmetic_css_for_url(&engine, "https://other.test/", &["generic-ad"], &[]).contains(".generic-ad"));
    }

    #[test]
    fn binary_snapshot_cache_and_opt_in_logger_work_and_recover_from_corruption() {
        let unique = format!(
            "superbird-cb-test-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        );
        let cache_dir = std::env::temp_dir().join(unique);
        let _ = std::fs::remove_dir_all(&cache_dir);

        let mut bundle = String::from("!superbird:meta version=1 mode=ubo logger=1\n");
        bundle.push_str(&make_bundle_list(
            "uboFilters",
            1,
            "standard",
            "||ads.example/banner.js$script\n##.ad-slot, .promo-slot\n",
        ));
        bundle.push_str("!superbird:end\n");

        // Build snapshot manually via helpers (avoids env var race with parallel tests)
        let parsed = parse_bundle(&bundle);
        let hash = fnv1a64_bundle_hash(&parsed);
        let cache_path = cache_dir.join(format!("engine-{:016x}.sbce", hash));

        let engine = create_engine(&bundle);
        assert!(engine.logger_enabled);
        let hit = check_network_request(
            &engine,
            "https://ads.example/banner.js",
            "https://site.test/",
            "script",
            "GET",
        );
        assert!(hit.matched);
        let log = filter_log_json(&engine);
        assert!(log.contains("\"action\":\"block\""));
        assert!(log.contains("||ads.example/banner.js$script"));

        // Save snapshot, reload from snapshot, verify identical blocking + generic selector rules
        try_save_snapshot(&cache_dir, &cache_path, &engine.engine, &engine.generic_selector_list_rules);
        let (reloaded_engine, reloaded_generic) = try_load_snapshot(&cache_path).expect("snapshot load");
        let req = Request::new("https://ads.example/banner.js", "https://site.test/", "script", "GET").unwrap();
        assert!(reloaded_engine.check_network_request(&req).should_block());
        assert!(!reloaded_generic.by_class.is_empty());

        // Corrupt snapshot must safely return None (no panic)
        std::fs::write(&cache_path, b"SBCE0001\x05\x00\x00\x00corrupt").unwrap();
        assert!(try_load_snapshot(&cache_path).is_none());

        let _ = std::fs::remove_dir_all(&cache_dir);
    }
}
