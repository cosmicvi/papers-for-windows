use std::path::Path;

fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
        let rc_path = Path::new(&manifest_dir).join("papers.rc");
        let ico_path = Path::new(&manifest_dir)
            .join("resources")
            .join("papers.ico");

        println!("cargo:rerun-if-changed={}", rc_path.display());
        println!("cargo:rerun-if-changed={}", ico_path.display());

        let mut res = winres::WindowsResource::new();
        res.set_resource_file(rc_path.to_str().unwrap());
        res.compile().expect("Failed to compile papers.rc resource");
    }
}
