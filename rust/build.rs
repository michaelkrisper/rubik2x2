//! Records the compiler version and optimisation settings so that `bench --json`
//! can report what actually produced the binary rather than a guess.

use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=build.rs");

    let rustc = std::env::var("RUSTC").unwrap_or_else(|_| "rustc".into());
    let version = Command::new(rustc)
        .arg("--version")
        .output()
        .ok()
        .and_then(|output| String::from_utf8(output.stdout).ok())
        .map(|text| {
            // "rustc 1.97.1 (8bab26f4f 2026-07-14)" -> "1.97.1"
            text.split_whitespace().nth(1).unwrap_or("unknown").to_owned()
        })
        .unwrap_or_else(|| "unknown".into());
    println!("cargo:rustc-env=RUSTC_VERSION={version}");

    let profile = std::env::var("PROFILE").unwrap_or_else(|_| "unknown".into());
    let opt_level = std::env::var("OPT_LEVEL").unwrap_or_else(|_| "?".into());
    println!("cargo:rustc-env=BUILD_PROFILE={profile}, opt-level={opt_level}");
}
