//! Prints how lyrics are split into syllables.   cargo run --release --example syls -- "some words"
use chimera_rack::*;
use std::ffi::{CStr, CString};
fn main() {
    for text in std::env::args().skip(1) {
        let c = CString::new(text.clone()).unwrap();
        let l = chim_lyrics_new(c.as_ptr(), 0.0, std::ptr::null());
        let mut out = Vec::new();
        for i in 0..chim_lyrics_len(l) {
            let mut buf = [0 as std::ffi::c_char; 32];
            chim_lyrics_text(l, i, buf.as_mut_ptr(), 32);
            out.push(unsafe { CStr::from_ptr(buf.as_ptr()) }.to_string_lossy().into_owned());
        }
        println!("{text}\n  {} syllables: {}", out.len(), out.join(" | "));
        chim_lyrics_free(l);
    }
}
