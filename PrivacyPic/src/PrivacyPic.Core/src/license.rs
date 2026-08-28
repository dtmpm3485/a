use base64::{engine::general_purpose::STANDARD, Engine as _};
use chrono::{DateTime, Duration, Utc};
use rsa::pkcs1v15::{Signature as RsaSignature, VerifyingKey};
use rsa::pkcs8::DecodePublicKey;
use rsa::signature::Verifier;
use rsa::RsaPublicKey;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicU32, Ordering};

#[cfg(windows)]
use winreg::enums::{HKEY_LOCAL_MACHINE, KEY_READ, KEY_WOW64_64KEY};
#[cfg(windows)]
use winreg::RegKey;

pub const FREE_LIMIT: u32 = 5;
static FREE_SANITIZE_COUNT: AtomicU32 = AtomicU32::new(0);

const PUBLIC_KEY_DER_B64: &str = "__PUBLIC_KEY_DER_B64__";

#[derive(Debug, Clone)]
pub struct LicenseClaims {
    pub license_id: String,
    pub order_id: String,
    pub issued: DateTime<Utc>,
    pub expires: Option<DateTime<Utc>>,
    pub device_code: String,
}

#[derive(Deserialize)]
struct Envelope {
    payload: String,
    signature: String,
}

pub fn consume_free_slot() -> bool {
    let prev = FREE_SANITIZE_COUNT.fetch_add(1, Ordering::SeqCst);
    prev < FREE_LIMIT
}

pub fn batch_limit(path: Option<&Path>) -> u32 {
    if let Some(p) = path {
        if verify_license_path(p).is_ok() {
            return i32::MAX as u32;
        }
    }
    FREE_LIMIT
}

pub fn verify_license_path(path: &Path) -> Result<LicenseClaims, i32> {
    let raw = fs::read_to_string(path).map_err(|_| -101)?;
    if raw.is_empty() || raw.len() > 64 * 1024 {
        return Err(-101);
    }

    let env: Envelope = serde_json::from_str(&raw).map_err(|_| -102)?;
    if env.payload.is_empty() || env.signature.is_empty() {
        return Err(-102);
    }

    let payload = STANDARD.decode(env.payload.as_bytes()).map_err(|_| -103)?;
    let signature_bytes = STANDARD.decode(env.signature.as_bytes()).map_err(|_| -103)?;

    let public_der = STANDARD.decode(PUBLIC_KEY_DER_B64.as_bytes()).map_err(|_| -104)?;
    let public_key = RsaPublicKey::from_public_key_der(&public_der).map_err(|_| -104)?;
    let verifier = VerifyingKey::<Sha256>::new(public_key);
    let signature = RsaSignature::try_from(signature_bytes.as_slice()).map_err(|_| -104)?;
    verifier.verify(&payload, &signature).map_err(|_| -104)?;

    let text = std::str::from_utf8(&payload).map_err(|_| -105)?;
    let parts: Vec<&str> = text.split('|').collect();

    if parts.len() != 7 || parts[0] != "PP2" {
        return Err(-105);
    }

    let license_id = parts[1];
    if license_id.len() != 32 || !license_id.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err(-106);
    }

    let order_id = parts[2].trim();
    if order_id.is_empty() || order_id.len() > 128 || order_id.contains('\r') || order_id.contains('\n') {
        return Err(-107);
    }

    if parts[3] != "PRO" {
        return Err(-108);
    }

    let issued = DateTime::parse_from_rfc3339(parts[4])
        .map_err(|_| -109)?
        .with_timezone(&Utc);
    let now = Utc::now();
    if issued > now + Duration::minutes(10) {
        return Err(-109);
    }

    let expires = if parts[5] == "NEVER" {
        None
    } else {
        let exp = DateTime::parse_from_rfc3339(parts[5])
            .map_err(|_| -109)?
            .with_timezone(&Utc);
        if exp <= now || exp <= issued {
            return Err(-109);
        }
        Some(exp)
    };

    let expected_device = device_code().map_err(|_| -110)?;
    if !constant_time_ascii_eq(parts[6], &expected_device) {
        return Err(-110);
    }

    Ok(LicenseClaims {
        license_id: license_id.to_ascii_uppercase(),
        order_id: order_id.to_owned(),
        issued,
        expires,
        device_code: expected_device,
    })
}

#[cfg(windows)]
pub fn device_code() -> Result<String, i32> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    let key = hklm
        .open_subkey_with_flags(
            "SOFTWARE\\Microsoft\\Cryptography",
            KEY_READ | KEY_WOW64_64KEY,
        )
        .map_err(|_| -201)?;
    let machine_guid: String = key.get_value("MachineGuid").map_err(|_| -201)?;

    let mut hasher = Sha256::new();
    hasher.update(b"PrivacyPic.Device.v2\0");
    hasher.update(machine_guid.trim().to_ascii_lowercase().as_bytes());
    let digest = hasher.finalize();

    let mut hex = String::with_capacity(16);
    for b in &digest[..8] {
        use std::fmt::Write;
        let _ = write!(&mut hex, "{:02X}", b);
    }

    Ok(format!(
        "PC-{}-{}-{}-{}",
        &hex[0..4],
        &hex[4..8],
        &hex[8..12],
        &hex[12..16]
    ))
}

#[cfg(not(windows))]
pub fn device_code() -> Result<String, i32> {
    Err(-201)
}

fn constant_time_ascii_eq(a: &str, b: &str) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.bytes().zip(b.bytes()) {
        diff |= x ^ y;
    }
    diff == 0
}
