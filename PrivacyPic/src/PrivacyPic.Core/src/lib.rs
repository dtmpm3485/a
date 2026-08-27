use std::fs;
use std::path::PathBuf;
use std::slice;

const FLAG_METADATA: u32 = 1 << 0;
const FLAG_GPS: u32 = 1 << 1;
const FLAG_DATETIME: u32 = 1 << 2;
const FLAG_DEVICE: u32 = 1 << 3;
const FLAG_SOFTWARE: u32 = 1 << 4;
const FLAG_XMP_IPTC: u32 = 1 << 5;

#[cfg(windows)]
fn path_from_utf16(ptr: *const u16) -> Result<PathBuf, ()> {
    use std::os::windows::ffi::OsStringExt;
    if ptr.is_null() { return Err(()); }
    let mut len = 0usize;
    unsafe {
        while *ptr.add(len) != 0 {
            len += 1;
            if len > 32767 { return Err(()); }
        }
        let s = slice::from_raw_parts(ptr, len);
        Ok(PathBuf::from(std::ffi::OsString::from_wide(s)))
    }
}

#[cfg(not(windows))]
fn path_from_utf16(_ptr: *const u16) -> Result<PathBuf, ()> { Err(()) }

fn ext_lower(path: &PathBuf) -> String {
    path.extension().and_then(|x| x.to_str()).unwrap_or("").to_ascii_lowercase()
}

#[no_mangle]
pub extern "C" fn pp_scan_file(path: *const u16) -> u32 {
    let path = match path_from_utf16(path) { Ok(p) => p, Err(_) => return 0 };
    let data = match fs::read(&path) { Ok(v) => v, Err(_) => return 0 };
    match ext_lower(&path).as_str() {
        "jpg" | "jpeg" => scan_jpeg(&data),
        "png" => scan_png(&data),
        "webp" => scan_webp(&data),
        _ => 0,
    }
}

#[no_mangle]
pub extern "C" fn pp_sanitize_file(input: *const u16, output: *const u16) -> i32 {
    let input = match path_from_utf16(input) { Ok(p) => p, Err(_) => return -1 };
    let output = match path_from_utf16(output) { Ok(p) => p, Err(_) => return -1 };
    let data = match fs::read(&input) { Ok(v) => v, Err(_) => return -2 };
    let result = match ext_lower(&input).as_str() {
        "jpg" | "jpeg" => sanitize_jpeg(&data),
        "png" => sanitize_png(&data),
        "webp" => sanitize_webp(&data),
        _ => return -3,
    };
    let out = match result { Ok(v) => v, Err(code) => return code };
    if let Some(parent) = output.parent() {
        if fs::create_dir_all(parent).is_err() { return -4; }
    }
    match fs::write(output, out) { Ok(_) => 0, Err(_) => -5 }
}

fn be_u16(d: &[u8], p: usize) -> Option<u16> {
    if p + 2 > d.len() { None } else { Some(u16::from_be_bytes([d[p], d[p+1]])) }
}

fn be_u32(d: &[u8], p: usize) -> Option<u32> {
    if p + 4 > d.len() { None } else { Some(u32::from_be_bytes([d[p],d[p+1],d[p+2],d[p+3]])) }
}

fn le_u32(d: &[u8], p: usize) -> Option<u32> {
    if p + 4 > d.len() { None } else { Some(u32::from_le_bytes([d[p],d[p+1],d[p+2],d[p+3]])) }
}

fn scan_jpeg(data: &[u8]) -> u32 {
    if data.len() < 4 || data[0] != 0xFF || data[1] != 0xD8 { return 0; }
    let mut flags = 0u32;
    let mut i = 2usize;

    while i + 1 < data.len() {
        if data[i] != 0xFF { i += 1; continue; }
        while i < data.len() && data[i] == 0xFF { i += 1; }
        if i >= data.len() { break; }

        let marker = data[i];
        i += 1;

        if marker == 0xD9 || marker == 0xDA { break; }
        if (0xD0..=0xD7).contains(&marker) || marker == 0x01 { continue; }

        let len = match be_u16(data, i) {
            Some(v) if v >= 2 => v as usize,
            _ => break
        };

        if i + len > data.len() { break; }
        let seg = &data[i+2..i+len];

        match marker {
            0xE1 => {
                flags |= FLAG_METADATA;
                if seg.starts_with(b"Exif\0\0") {
                    flags |= scan_tiff(&seg[6..]);
                } else {
                    flags |= FLAG_XMP_IPTC;
                }
            }
            0xED => flags |= FLAG_METADATA | FLAG_XMP_IPTC,
            0xFE => flags |= FLAG_METADATA | FLAG_SOFTWARE,
            _ => {}
        }

        i += len;
    }

    flags
}

fn sanitize_jpeg(data: &[u8]) -> Result<Vec<u8>, i32> {
    if data.len() < 4 || data[0] != 0xFF || data[1] != 0xD8 { return Err(-10); }

    let mut out = Vec::with_capacity(data.len());
    out.extend_from_slice(&data[0..2]);

    let mut i = 2usize;
    while i < data.len() {
        if i + 1 >= data.len() {
            out.extend_from_slice(&data[i..]);
            break;
        }

        if data[i] != 0xFF {
            out.push(data[i]);
            i += 1;
            continue;
        }

        let marker_start = i;
        while i < data.len() && data[i] == 0xFF { i += 1; }

        if i >= data.len() {
            out.extend_from_slice(&data[marker_start..]);
            break;
        }

        let marker = data[i];
        i += 1;

        if marker == 0xDA {
            out.extend_from_slice(&data[marker_start..]);
            break;
        }

        if marker == 0xD9 {
            out.extend_from_slice(&data[marker_start..i]);
            break;
        }

        if (0xD0..=0xD7).contains(&marker) || marker == 0x01 {
            out.extend_from_slice(&data[marker_start..i]);
            continue;
        }

        let len = match be_u16(data, i) {
            Some(v) if v >= 2 => v as usize,
            _ => return Err(-11)
        };

        if i + len > data.len() { return Err(-11); }

        let should_drop = marker == 0xE1 || marker == 0xED || marker == 0xFE;
        if !should_drop {
            out.extend_from_slice(&data[marker_start..i+len]);
        }

        i += len;
    }

    Ok(out)
}

fn scan_png(data: &[u8]) -> u32 {
    const SIG: &[u8;8] = b"\x89PNG\r\n\x1a\n";
    if data.len() < 8 || &data[..8] != SIG { return 0; }

    let mut flags = 0u32;
    let mut i = 8usize;

    while i + 12 <= data.len() {
        let len = match be_u32(data, i) {
            Some(v) => v as usize,
            None => break
        };

        if i + 12 + len > data.len() { break; }

        let kind = &data[i+4..i+8];
        let chunk_data = &data[i+8..i+8+len];

        if kind == b"eXIf" {
            flags |= FLAG_METADATA | scan_tiff(chunk_data);
        } else if kind == b"tEXt" || kind == b"zTXt" || kind == b"iTXt" {
            flags |= FLAG_METADATA | FLAG_XMP_IPTC;
        } else if kind == b"tIME" {
            flags |= FLAG_METADATA | FLAG_DATETIME;
        }

        i += 12 + len;
        if kind == b"IEND" { break; }
    }

    flags
}

fn sanitize_png(data: &[u8]) -> Result<Vec<u8>, i32> {
    const SIG: &[u8;8] = b"\x89PNG\r\n\x1a\n";
    if data.len() < 8 || &data[..8] != SIG { return Err(-20); }

    let mut out = Vec::with_capacity(data.len());
    out.extend_from_slice(SIG);

    let mut i = 8usize;
    while i + 12 <= data.len() {
        let len = be_u32(data, i).ok_or(-21)? as usize;
        if i + 12 + len > data.len() { return Err(-21); }

        let kind = &data[i+4..i+8];
        let drop = kind == b"eXIf"
            || kind == b"tEXt"
            || kind == b"zTXt"
            || kind == b"iTXt"
            || kind == b"tIME";

        if !drop {
            out.extend_from_slice(&data[i..i+12+len]);
        }

        i += 12 + len;
        if kind == b"IEND" { break; }
    }

    Ok(out)
}

fn scan_webp(data: &[u8]) -> u32 {
    if data.len() < 12 || &data[0..4] != b"RIFF" || &data[8..12] != b"WEBP" { return 0; }

    let mut flags = 0u32;
    let mut i = 12usize;

    while i + 8 <= data.len() {
        let kind = &data[i..i+4];
        let len = match le_u32(data, i+4) {
            Some(v) => v as usize,
            None => break
        };

        if i + 8 + len > data.len() { break; }
        let chunk = &data[i+8..i+8+len];

        if kind == b"EXIF" {
            flags |= FLAG_METADATA | scan_tiff(chunk);
        } else if kind == b"XMP " {
            flags |= FLAG_METADATA | FLAG_XMP_IPTC;
        }

        i += 8 + len + (len & 1);
    }

    flags
}

fn sanitize_webp(data: &[u8]) -> Result<Vec<u8>, i32> {
    if data.len() < 12 || &data[0..4] != b"RIFF" || &data[8..12] != b"WEBP" { return Err(-30); }

    let mut chunks: Vec<Vec<u8>> = Vec::new();
    let mut i = 12usize;

    while i + 8 <= data.len() {
        let kind = &data[i..i+4];
        let len = le_u32(data, i+4).ok_or(-31)? as usize;
        let padded = len + (len & 1);

        if i + 8 + padded > data.len() { return Err(-31); }

        if kind != b"EXIF" && kind != b"XMP " {
            let mut c = data[i..i+8+padded].to_vec();

            if kind == b"VP8X" && len >= 1 {
                c[8] &= !0x0C;
            }

            chunks.push(c);
        }

        i += 8 + padded;
    }

    let body_len: usize = 4 + chunks.iter().map(|c| c.len()).sum::<usize>();
    let mut out = Vec::with_capacity(8 + body_len);

    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(body_len as u32).to_le_bytes());
    out.extend_from_slice(b"WEBP");

    for c in chunks {
        out.extend_from_slice(&c);
    }

    Ok(out)
}

fn scan_tiff(data: &[u8]) -> u32 {
    if data.len() < 8 { return 0; }

    let little = if &data[0..2] == b"II" {
        true
    } else if &data[0..2] == b"MM" {
        false
    } else {
        return 0;
    };

    let read16 = |p:usize| -> Option<u16> {
        if p+2 > data.len() {
            None
        } else if little {
            Some(u16::from_le_bytes([data[p],data[p+1]]))
        } else {
            Some(u16::from_be_bytes([data[p],data[p+1]]))
        }
    };

    let read32 = |p:usize| -> Option<u32> {
        if p+4 > data.len() {
            None
        } else if little {
            Some(u32::from_le_bytes([data[p],data[p+1],data[p+2],data[p+3]]))
        } else {
            Some(u32::from_be_bytes([data[p],data[p+1],data[p+2],data[p+3]]))
        }
    };

    if read16(2) != Some(42) { return 0; }

    let first = match read32(4) {
        Some(v) => v as usize,
        None => return 0
    };

    let mut flags = 0u32;
    let mut stack = vec![first];
    let mut visited: Vec<usize> = Vec::new();

    while let Some(off) = stack.pop() {
        if visited.contains(&off) || visited.len() > 16 || off + 2 > data.len() {
            continue;
        }

        visited.push(off);

        let count = match read16(off) {
            Some(v) => v as usize,
            None => continue
        };

        for n in 0..count.min(4096) {
            let e = off + 2 + n*12;
            if e + 12 > data.len() { break; }

            let tag = match read16(e) {
                Some(v) => v,
                None => continue
            };

            let value = read32(e+8).unwrap_or(0) as usize;

            match tag {
                0x8825 => {
                    flags |= FLAG_GPS;
                    if value < data.len() { stack.push(value); }
                }
                0x8769 | 0xA005 => {
                    if value < data.len() { stack.push(value); }
                }
                0x010F | 0x0110 | 0xA434 => flags |= FLAG_DEVICE,
                0x0132 | 0x9003 | 0x9004 => flags |= FLAG_DATETIME,
                0x0131 | 0x9286 | 0x9C9B | 0x9C9C | 0x9C9D | 0x9C9E | 0x9C9F => flags |= FLAG_SOFTWARE,
                _ => {}
            }
        }

        let next_pos = off + 2 + count.saturating_mul(12);
        if let Some(next) = read32(next_pos) {
            if next != 0 && (next as usize) < data.len() {
                stack.push(next as usize);
            }
        }
    }

    flags
}
