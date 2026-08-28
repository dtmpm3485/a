use base64::{engine::general_purpose::STANDARD, Engine as _};
use rsa::{
    pkcs1v15::{Signature as RsaSignature, VerifyingKey},
    pkcs8::DecodePublicKey,
    RsaPublicKey,
};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use rsa::signature::Verifier;
use std::{
    collections::BTreeMap,
    env,
    ffi::c_void,
    fs,
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    ptr,
    slice,
    sync::atomic::{AtomicBool, Ordering},
    time::{SystemTime, UNIX_EPOCH},
};

pub const ALL_SECURITY_BITS: u32 = 0x03FF;
static INTEGRITY_OK: AtomicBool = AtomicBool::new(false);

const PUBLIC_KEY_PEM: &str = r#"__PUBLIC_KEY_PEM__"#;

#[derive(Deserialize)]
struct LicenseEnvelope {
    version: u32,
    payload: String,
    signature: String,
}

#[derive(Deserialize)]
struct IntegrityManifest {
    version: u32,
    files: BTreeMap<String, String>,
}

#[link(name = "kernel32")]
extern "system" {
    fn LoadLibraryW(name: *const u16) -> *mut c_void;
    fn GetProcAddress(module: *mut c_void, name: *const u8) -> *mut c_void;
}

type GuardDeviceFn = unsafe extern "C" fn(*mut u16, usize) -> i32;
type GuardSimpleFn = unsafe extern "C" fn() -> i32;

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn app_dir() -> Option<PathBuf> {
    env::current_exe().ok()?.parent().map(Path::to_path_buf)
}

fn local_dir() -> Option<PathBuf> {
    let base = env::var_os("LOCALAPPDATA")?;
    Some(PathBuf::from(base).join("PrivacyPic"))
}

fn license_path() -> Option<PathBuf> {
    Some(local_dir()?.join("license.lic"))
}

fn load_guard() -> Option<*mut c_void> {
    let path = app_dir()?.join("privacypic_guard.dll");
    let mut wide: Vec<u16> = path.as_os_str().encode_wide().collect();
    wide.push(0);
    let h = unsafe { LoadLibraryW(wide.as_ptr()) };
    if h.is_null() { None } else { Some(h) }
}

fn guard_device_id() -> Option<String> {
    let h = load_guard()?;
    let p = unsafe { GetProcAddress(h, b"pp_guard_get_device_id\0".as_ptr()) };
    if p.is_null() { return None; }
    let f: GuardDeviceFn = unsafe { std::mem::transmute(p) };
    let mut buf = vec![0u16; 128];
    let len = unsafe { f(buf.as_mut_ptr(), buf.len()) };
    if len <= 0 || len as usize >= buf.len() { return None; }
    Some(String::from_utf16_lossy(&buf[..len as usize]))
}

fn guard_launcher_ok() -> bool {
    let h = match load_guard() { Some(v) => v, None => return false };
    let p = unsafe { GetProcAddress(h, b"pp_guard_launcher_ok\0".as_ptr()) };
    if p.is_null() { return false; }
    let f: GuardSimpleFn = unsafe { std::mem::transmute(p) };
    unsafe { f() == 1 }
}

fn verify_signature(payload: &[u8], signature: &[u8]) -> bool {
    let key = match RsaPublicKey::from_public_key_pem(PUBLIC_KEY_PEM) {
        Ok(k) => k,
        Err(_) => return false,
    };
    let verifier = VerifyingKey::<Sha256>::new(key);
    let sig = match RsaSignature::try_from(signature) {
        Ok(s) => s,
        Err(_) => return false,
    };
    verifier.verify(payload, &sig).is_ok()
}

fn verify_clock(now: i64) -> bool {
    let dir = match local_dir() { Some(v) => v, None => return false };
    let path = dir.join("clock.state");
    if let Ok(s) = fs::read_to_string(&path) {
        if let Ok(last) = s.trim().parse::<i64>() {
            if now + 300 < last {
                return false;
            }
        }
    }
    true
}

fn update_clock(now: i64) {
    if let Some(dir) = local_dir() {
        let _ = fs::create_dir_all(&dir);
        let _ = fs::write(dir.join("clock.state"), now.to_string());
    }
}

fn verify_integrity() -> (bool, bool) {
    let dir = match app_dir() { Some(v) => v, None => return (false, false) };
    let manifest_path = dir.join("integrity.json");
    let sig_path = dir.join("integrity.sig");

    let raw = match fs::read(&manifest_path) {
        Ok(v) => v,
        Err(_) => return (false, false),
    };
    let sig_text = match fs::read_to_string(&sig_path) {
        Ok(v) => v,
        Err(_) => return (false, false),
    };
    let sig = match STANDARD.decode(sig_text.trim()) {
        Ok(v) => v,
        Err(_) => return (false, false),
    };

    let signature_ok = verify_signature(&raw, &sig);
    if !signature_ok {
        return (false, false);
    }

    let manifest: IntegrityManifest = match serde_json::from_slice(&raw) {
        Ok(v) => v,
        Err(_) => return (true, false),
    };
    if manifest.version != 1 {
        return (true, false);
    }

    for (name, expected) in manifest.files {
        if name.contains('/') || name.contains('\\') || name.contains("..") {
            return (true, false);
        }
        let data = match fs::read(dir.join(name)) {
            Ok(v) => v,
            Err(_) => return (true, false),
        };
        let actual = format!("{:x}", Sha256::digest(&data));
        if !actual.eq_ignore_ascii_case(expected.trim()) {
            return (true, false);
        }
    }

    (true, true)
}

fn evaluate_text(text: &str, full_integrity: bool, write_clock: bool) -> u32 {
    let mut bits = 0u32;
    let envl: LicenseEnvelope = match serde_json::from_str(text) {
        Ok(v) => v,
        Err(_) => return bits,
    };

    if envl.version == 2 && !envl.payload.is_empty() && !envl.signature.is_empty() {
        bits |= 1 << 0;
    } else {
        return bits;
    }

    let payload = match STANDARD.decode(&envl.payload) {
        Ok(v) => v,
        Err(_) => return bits,
    };
    let signature = match STANDARD.decode(&envl.signature) {
        Ok(v) => v,
        Err(_) => return bits,
    };

    if verify_signature(&payload, &signature) {
        bits |= 1 << 1;
    } else {
        return bits;
    }

    let raw = match String::from_utf8(payload) {
        Ok(v) => v,
        Err(_) => return bits,
    };
    let parts: Vec<&str> = raw.split('|').collect();

    if parts.len() == 8
        && parts[0] == "PP2"
        && parts[3] == "PRO"
        && parts[7].contains("BATCH")
        && parts[7].contains("FOLDER")
    {
        bits |= 1 << 2;
    } else {
        return bits;
    }

    if parts[1].len() >= 16
        && parts[1].len() <= 64
        && !parts[2].trim().is_empty()
        && parts[2].len() <= 128
    {
        bits |= 1 << 3;
    } else {
        return bits;
    }

    let now = now_unix();
    let issued = match parts[4].parse::<i64>() {
        Ok(v) => v,
        Err(_) => return bits,
    };
    if issued > 1_700_000_000 && issued <= now + 300 {
        bits |= 1 << 4;
    } else {
        return bits;
    }

    let expires = match parts[5].parse::<i64>() {
        Ok(v) => v,
        Err(_) => return bits,
    };
    if (expires == 0 || expires > now) && verify_clock(now) {
        bits |= 1 << 5;
    } else {
        return bits;
    }

    let current_device = match guard_device_id() {
        Some(v) => v,
        None => return bits,
    };
    if parts[6] == current_device {
        bits |= 1 << 6;
    } else {
        return bits;
    }

    if guard_launcher_ok() {
        bits |= 1 << 7;
    } else {
        return bits;
    }

    if full_integrity {
        let (sig_ok, hashes_ok) = verify_integrity();
        if sig_ok {
            bits |= 1 << 8;
        } else {
            return bits;
        }
        if hashes_ok {
            bits |= 1 << 9;
        } else {
            return bits;
        }
    } else if INTEGRITY_OK.load(Ordering::SeqCst) {
        bits |= (1 << 8) | (1 << 9);
    } else {
        return bits;
    }

    if bits == ALL_SECURITY_BITS && write_clock {
        update_clock(now);
        INTEGRITY_OK.store(true, Ordering::SeqCst);
    }

    bits
}

pub fn security_flags(full_integrity: bool) -> u32 {
    let path = match license_path() { Some(v) => v, None => return 0 };
    let text = match fs::read_to_string(path) {
        Ok(v) => v,
        Err(_) => return 0,
    };
    evaluate_text(&text, full_integrity, true)
}

pub fn is_pro_fast() -> bool {
    security_flags(false) == ALL_SECURITY_BITS
}

pub fn device_id() -> String {
    guard_device_id().unwrap_or_else(|| "UNAVAILABLE".to_string())
}

pub fn install_license(source: &Path) -> i32 {
    let text = match fs::read_to_string(source) {
        Ok(v) => v,
        Err(_) => return -201,
    };

    if evaluate_text(&text, true, false) != ALL_SECURITY_BITS {
        return -202;
    }

    let dest = match license_path() {
        Some(v) => v,
        None => return -203,
    };

    if let Some(parent) = dest.parent() {
        if fs::create_dir_all(parent).is_err() {
            return -204;
        }
    }

    if fs::write(&dest, text).is_err() {
        return -205;
    }

    let bits = security_flags(true);
    if bits == ALL_SECURITY_BITS { 0 } else { -206 }
}

pub unsafe fn write_utf16(output: *mut u16, capacity: usize, text: &str) -> i32 {
    if output.is_null() || capacity == 0 {
        return -1;
    }
    let mut w: Vec<u16> = text.encode_utf16().collect();
    if w.len() + 1 > capacity {
        return -2;
    }
    w.push(0);
    ptr::copy_nonoverlapping(w.as_ptr(), output, w.len());
    (w.len() - 1) as i32
}

pub unsafe fn path_from_wide(ptr_in: *const u16) -> Option<PathBuf> {
    if ptr_in.is_null() {
        return None;
    }
    let mut len = 0usize;
    while *ptr_in.add(len) != 0 {
        len += 1;
        if len > 32767 {
            return None;
        }
    }
    let slice = slice::from_raw_parts(ptr_in, len);
    Some(PathBuf::from(std::ffi::OsString::from_wide(slice)))
}
