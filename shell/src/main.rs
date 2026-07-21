#![windows_subsystem = "windows"]

mod config {
    #![allow(dead_code)]

    include!(concat!(env!("CODEGEN_BUILD_DIR"), "/config.rs"));
}

use config::GETTEXT_PACKAGE;

mod annotation_properties_dialog;
mod application;
mod deps;
mod document_view;
mod file_monitor;
mod find_sidebar;
mod ink_transformation;
#[cfg(feature = "with-keyring")]
mod keyring;
mod loader_view;
mod page_selector;
mod password_view;
mod progress_message_area;
mod properties_fonts;
mod properties_general;
mod properties_license;
mod properties_signatures;
mod properties_window;
mod search_box;
mod sidebar;
mod sidebar_annotations;
mod sidebar_annotations_row;
mod sidebar_attachments;
mod sidebar_layers;
mod sidebar_links;
mod sidebar_page;
mod sidebar_thumbnails;
mod sign_manually_dialog;
mod signature_drawing_widget;
mod signature_image_processing;
mod signature_manager;
mod simple_color_swatch;
mod thumbnail_item;
mod visual_signature_chooser;
mod window;

use deps::*;

fn main() -> glib::ExitCode {
    #[cfg(target_os = "windows")]
    {
        if std::env::var_os("DBUS_SESSION_BUS_ADDRESS").is_none() {
            unsafe {
                std::env::set_var("DBUS_SESSION_BUS_ADDRESS", "none");
            }
        }
        unsafe {
            std::env::set_var("GTK_USE_PORTAL", "0");
            std::env::set_var("GSETTINGS_BACKEND", "memory");
        }
    }

    let mut log_builder = env_logger::builder();
    log_builder.format_timestamp_millis();

    if !glib::log_writer_default_would_drop(glib::LogLevel::Debug, Some("papers")) {
        log_builder.filter_module("papers", log::LevelFilter::Debug);
    }

    log_builder.init();

    gettextrs::bindtextdomain(GETTEXT_PACKAGE, PPS_LOCALEDIR)
        .expect("Unable to bind the text domain");
    gettextrs::bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8")
        .expect("Unable to bind the text domain codeset");
    gettextrs::textdomain(GETTEXT_PACKAGE).expect("Unable to switch to the text domain");

    // Register an ink transformation function that should live in libdocument but uses various rust crates
    ink_transformation::setup();

    PpsApplication::new().run()
}
