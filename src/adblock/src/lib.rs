//! A C interface to Brave's adblock-rust for Brouk (specs/007).
//!
//! Brave's own C interface (adblock-rust-ffi) is archived, so this thin
//! wrapper exposes what the browser needs. The matching C header is
//! `include/brouk_adblock.h`; the C++ side wraps it in RAII types
//! (src/core/adblock_engine.h). Every function catches panics: none may
//! unwind into C++ (the crate is also built with `panic = "abort"`).
//!
//! Ownership: strings and byte buffers returned here belong to the caller,
//! who frees them with `brouk_adblock_free_string` / `_free_bytes`. Inputs
//! are borrowed for the duration of the call only.

use std::collections::HashSet;
use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::slice;

use adblock::lists::{FilterFormat, FilterSet, ParseOptions, ParsedLine};
use adblock::request::Request;
use adblock::resources::{PermissionMask, Resource};
use adblock::sourcemap::FilterRuleDebugInfo;
use adblock::Engine;

/// The engine, opaque to C.
pub struct BroukAdblockEngine {
    engine: Engine,
}

/// One filter list for `brouk_adblock_engine_build`.
#[repr(C)]
pub struct BroukAdblockList {
    pub text: *const u8,
    pub len: usize,
    /// 0: Adblock Plus / uBlock Origin syntax, 1: hosts file or plain domains.
    pub format: u8,
    /// Non-zero: the list may use scriptlets that need trust (uBlock's own
    /// lists and the user's filters).
    pub trusted: u8,
}

/// What `brouk_adblock_check` found for a request.
#[repr(C)]
pub struct BroukAdblockMatch {
    /// The request should be blocked.
    pub block: u8,
    /// A rule with `$important` matched (exceptions do not apply).
    pub important: u8,
    pub has_filter: u8,
    pub has_exception: u8,
    /// Which list and line (zero-based) matched; -1 when unknown.
    pub filter_list: i32,
    pub filter_line: i32,
    pub exception_list: i32,
    pub exception_line: i32,
    /// The matching rules as written; NULL when unknown.
    pub filter_text: *mut c_char,
    pub exception_text: *mut c_char,
    /// A data: URL to serve instead (`$redirect`); NULL for none.
    pub redirect: *mut c_char,
    /// The URL without removed parameters (`$removeparam`); NULL if unchanged.
    pub rewritten_url: *mut c_char,
}

const ALL_PERMISSIONS: u8 = 0xff;

fn to_c_string(text: String) -> *mut c_char {
    // Interior NUL bytes cannot cross into C; drop them.
    let clean: Vec<u8> = text.into_bytes().into_iter().filter(|b| *b != 0).collect();
    CString::new(clean).map_or(ptr::null_mut(), CString::into_raw)
}

/// # Safety
/// `text` is NULL or a NUL-terminated string valid for the call.
unsafe fn borrow_str<'a>(text: *const c_char) -> Option<&'a str> {
    if text.is_null() {
        return None;
    }
    CStr::from_ptr(text).to_str().ok()
}

/// # Safety
/// `data` is NULL (with `len` 0) or points to `len` readable bytes.
unsafe fn borrow_bytes<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if data.is_null() || len == 0 {
        &[]
    } else {
        slice::from_raw_parts(data, len)
    }
}

fn location(info: &Option<FilterRuleDebugInfo>) -> (i32, i32, *mut c_char) {
    match info {
        None => (-1, -1, ptr::null_mut()),
        Some(info) => {
            let (list, line) = info
                .source_location
                .as_ref()
                .map_or((-1, -1), |l| (l.source_index as i32, l.line_number as i32));
            let text = info.raw_line.clone().map_or(ptr::null_mut(), to_c_string);
            (list, line, text)
        }
    }
}

/// Builds an engine from lists, in order (their index is the list number in
/// matches). Returns NULL on failure.
///
/// # Safety
/// `lists` points to `count` valid `BroukAdblockList` values whose texts are
/// readable for the call.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_engine_build(
    lists: *const BroukAdblockList,
    count: usize,
) -> *mut BroukAdblockEngine {
    catch_unwind(AssertUnwindSafe(|| {
        let lists = if lists.is_null() || count == 0 {
            &[][..]
        } else {
            slice::from_raw_parts(lists, count)
        };
        // Debug mode keeps each rule's list and line, for the blocked
        // requests list.
        let mut set = FilterSet::new(true);
        for list in lists {
            let text = String::from_utf8_lossy(borrow_bytes(list.text, list.len)).into_owned();
            let options = ParseOptions {
                format: if list.format == 1 {
                    FilterFormat::Hosts
                } else {
                    FilterFormat::Standard
                },
                permissions: PermissionMask::from_bits(if list.trusted != 0 {
                    ALL_PERMISSIONS
                } else {
                    0
                }),
                ..ParseOptions::default()
            };
            set.add_filter_list(text, options);
        }
        let engine = Engine::new_with_filter_set(set);
        Box::into_raw(Box::new(BroukAdblockEngine { engine }))
    }))
    .unwrap_or(ptr::null_mut())
}

/// An engine from `brouk_adblock_engine_serialize` output (the disk cache).
/// Returns NULL when the data is not a valid engine of this version.
///
/// # Safety
/// `data` points to `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_engine_deserialize(
    data: *const u8,
    len: usize,
) -> *mut BroukAdblockEngine {
    catch_unwind(AssertUnwindSafe(|| {
        let mut engine = Engine::default();
        match engine.deserialize(borrow_bytes(data, len)) {
            Ok(()) => Box::into_raw(Box::new(BroukAdblockEngine { engine })),
            Err(_) => ptr::null_mut(),
        }
    }))
    .unwrap_or(ptr::null_mut())
}

/// The engine as bytes, for the disk cache. Free with
/// `brouk_adblock_free_bytes`.
///
/// # Safety
/// `engine` is a live engine; `len` is writable.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_engine_serialize(
    engine: *const BroukAdblockEngine,
    len: *mut usize,
) -> *mut u8 {
    if engine.is_null() || len.is_null() {
        return ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        let bytes = (*engine).engine.serialize().into_boxed_slice();
        *len = bytes.len();
        Box::into_raw(bytes) as *mut u8
    }))
    .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `engine` came from this library and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_engine_free(engine: *mut BroukAdblockEngine) {
    if !engine.is_null() {
        drop(Box::from_raw(engine));
    }
}

/// Loads redirect and scriptlet resources (adblock-resources'
/// resources.json). Returns 0 when the JSON could not be read.
///
/// # Safety
/// `engine` is a live engine not used by other threads during the call;
/// `json` points to `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_use_resources(
    engine: *mut BroukAdblockEngine,
    json: *const u8,
    len: usize,
) -> u8 {
    if engine.is_null() {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| {
        match serde_json::from_slice::<Vec<Resource>>(borrow_bytes(json, len)) {
            Ok(resources) => {
                (*engine).engine.use_resources(resources);
                1
            }
            Err(_) => 0,
        }
    }))
    .unwrap_or(0)
}

/// Checks a request. `request_type` is one of adblock-rust's types
/// ("document", "subdocument", "script", "image", "stylesheet", "xhr",
/// "font", "media", "websocket", "ping", "other"…). Clear `out` with
/// `brouk_adblock_match_clear`.
///
/// # Safety
/// `engine` is a live engine; the strings are NUL-terminated; `out` is
/// writable.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_check(
    engine: *const BroukAdblockEngine,
    url: *const c_char,
    source_url: *const c_char,
    request_type: *const c_char,
    out: *mut BroukAdblockMatch,
) {
    if out.is_null() {
        return;
    }
    *out = BroukAdblockMatch {
        block: 0,
        important: 0,
        has_filter: 0,
        has_exception: 0,
        filter_list: -1,
        filter_line: -1,
        exception_list: -1,
        exception_line: -1,
        filter_text: ptr::null_mut(),
        exception_text: ptr::null_mut(),
        redirect: ptr::null_mut(),
        rewritten_url: ptr::null_mut(),
    };
    if engine.is_null() {
        return;
    }
    let (Some(url), Some(source_url), Some(request_type)) =
        (borrow_str(url), borrow_str(source_url), borrow_str(request_type))
    else {
        return;
    };
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Ok(request) = Request::new(url, source_url, request_type, "GET") else {
            return;
        };
        let result = (*engine).engine.check_network_request(&request);
        let (filter_list, filter_line, filter_text) = location(&result.filter);
        let (exception_list, exception_line, exception_text) = location(&result.exception);
        *out = BroukAdblockMatch {
            block: u8::from(result.should_block()),
            important: u8::from(result.important),
            has_filter: u8::from(result.filter.is_some()),
            has_exception: u8::from(result.exception.is_some()),
            filter_list,
            filter_line,
            exception_list,
            exception_line,
            filter_text,
            exception_text,
            redirect: result.redirect.map_or(ptr::null_mut(), to_c_string),
            rewritten_url: result.rewritten_url.map_or(ptr::null_mut(), to_c_string),
        };
    }));
}

/// Frees the strings of a match (it can be reused afterwards).
///
/// # Safety
/// `match_` was filled by `brouk_adblock_check`.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_match_clear(match_: *mut BroukAdblockMatch) {
    if match_.is_null() {
        return;
    }
    for field in [
        &mut (*match_).filter_text,
        &mut (*match_).exception_text,
        &mut (*match_).redirect,
        &mut (*match_).rewritten_url,
    ] {
        if !field.is_null() {
            drop(CString::from_raw(*field));
            *field = ptr::null_mut();
        }
    }
}

/// The cosmetic resources of a page, as JSON:
/// {"hide_selectors": [...], "procedural_actions": [...], "exceptions":
/// [...], "injected_script": "...", "generichide": bool}.
///
/// # Safety
/// `engine` is a live engine; `url` is NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_url_cosmetic_resources(
    engine: *const BroukAdblockEngine,
    url: *const c_char,
) -> *mut c_char {
    if engine.is_null() {
        return ptr::null_mut();
    }
    let Some(url) = borrow_str(url) else {
        return ptr::null_mut();
    };
    catch_unwind(AssertUnwindSafe(|| {
        let resources = (*engine).engine.url_cosmetic_resources(url);
        let json = serde_json::json!({
            "hide_selectors": resources.hide_selectors,
            "procedural_actions": resources.procedural_actions,
            "exceptions": resources.exceptions,
            "injected_script": resources.injected_script,
            "generichide": resources.generichide,
        });
        to_c_string(json.to_string())
    }))
    .unwrap_or(ptr::null_mut())
}

/// Generic hiding selectors for classes and ids seen on a page. `json` is
/// {"classes": [...], "ids": [...], "exceptions": [...]}; the answer is a
/// JSON array of selectors.
///
/// # Safety
/// `engine` is a live engine; `json` points to `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_hidden_class_id_selectors(
    engine: *const BroukAdblockEngine,
    json: *const u8,
    len: usize,
) -> *mut c_char {
    if engine.is_null() {
        return ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        let value: serde_json::Value =
            serde_json::from_slice(borrow_bytes(json, len)).unwrap_or_default();
        let strings = |key: &str| -> Vec<String> {
            value
                .get(key)
                .and_then(|v| v.as_array())
                .map(|items| {
                    items
                        .iter()
                        .filter_map(|v| v.as_str().map(str::to_owned))
                        .collect()
                })
                .unwrap_or_default()
        };
        let exceptions: HashSet<String> = strings("exceptions").into_iter().collect();
        let selectors =
            (*engine)
                .engine
                .hidden_class_id_selectors(strings("classes"), strings("ids"), &exceptions);
        to_c_string(serde_json::Value::from(selectors).to_string())
    }))
    .unwrap_or(ptr::null_mut())
}

/// Checks one line of the user's filters. Returns 0 for a valid rule, 1 for
/// a comment or empty line, 2 for an invalid or unsupported rule (then
/// `error`, if not NULL, gets a message to free).
///
/// # Safety
/// `line` is NUL-terminated; `error` is NULL or writable.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_check_filter(
    line: *const c_char,
    error: *mut *mut c_char,
) -> u8 {
    if !error.is_null() {
        *error = ptr::null_mut();
    }
    let Some(line) = borrow_str(line) else {
        return 2;
    };
    let trimmed = line.trim();
    if trimmed.is_empty() || trimmed.starts_with('!') || trimmed.starts_with('[') {
        return 1;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let options = ParseOptions {
            permissions: PermissionMask::from_bits(ALL_PERMISSIONS),
            ..ParseOptions::default()
        };
        let message = match adblock::lists::parse_filter(trimmed, false, options) {
            Ok(ParsedLine::Network(_)) | Ok(ParsedLine::Cosmetic(_)) => return 0,
            Err(adblock::lists::FilterParseError::Unsupported) => {
                "This kind of rule is not supported.".to_owned()
            }
            Err(e) => {
                let text = e.to_string();
                let mut chars = text.chars();
                match chars.next() {
                    Some(first) => first.to_uppercase().collect::<String>() + chars.as_str(),
                    None => "Invalid rule.".to_owned(),
                }
            }
        };
        if !error.is_null() {
            *error = to_c_string(message);
        }
        2
    }))
    .unwrap_or(2)
}

/// # Safety
/// `text` came from this library (or is NULL).
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_free_string(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}

/// # Safety
/// `data` and `len` came from `brouk_adblock_engine_serialize`.
#[no_mangle]
pub unsafe extern "C" fn brouk_adblock_free_bytes(data: *mut u8, len: usize) {
    if !data.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(data, len)));
    }
}
