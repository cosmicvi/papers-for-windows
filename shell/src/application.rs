use crate::deps::*;

use papers_document::{LinkDest, LinkDestType};
use papers_view::Job;

use git_version::git_version;
use std::env;
use std::ffi::OsString;
use std::ops::ControlFlow;

const RESOURCES_DATA: &[u8] = include_bytes!(env!("PAPERS_RESOURCES_FILE"));

mod imp {
    use super::*;

    #[derive(Default)]
    pub struct PpsApplication;

    #[glib::object_subclass]
    impl ObjectSubclass for PpsApplication {
        const NAME: &'static str = "PpsApplication";
        type Type = super::PpsApplication;
        type ParentType = adw::Application;
    }

    impl ObjectImpl for PpsApplication {
        fn constructed(&self) {
            self.parent_constructed();
            self.setup_command_line();
        }
    }

    impl AdwApplicationImpl for PpsApplication {}

    impl GtkApplicationImpl for PpsApplication {}

    impl ApplicationImpl for PpsApplication {
        fn startup(&self) {
            let resources = gio::Resource::from_data(&glib::Bytes::from_static(RESOURCES_DATA))
                .expect("failed to load resources");
            gio::resources_register(&resources);

            self.parent_startup();
            papers_document::init();

            // Manually set name and icon
            glib::set_application_name(&gettext("Document Viewer"));
            gtk::Window::set_default_icon_name(APP_ID);

            self.setup_actions();
        }

        fn shutdown(&self) {
            papers_document::shutdown();
            Job::scheduler_wait();
            self.parent_shutdown();
        }

        fn activate(&self) {
            for window in self.obj().windows() {
                if let Ok(window) = window.downcast::<PpsWindow>() {
                    window.present();
                }
            }
        }

        fn handle_local_options(&self, options: &glib::VariantDict) -> ControlFlow<glib::ExitCode> {
            // print the version in local instance rather than sending it to primary
            if options.contains("version") {
                glib::g_print!(
                    "{} {}\n",
                    gettext("Papers Document Viewer"),
                    crate::config::VERSION
                );
                return ControlFlow::Break(glib::ExitCode::SUCCESS);
            }

            ControlFlow::Continue(())
        }

        fn command_line(&self, command_line: &gio::ApplicationCommandLine) -> glib::ExitCode {
            let options = command_line.options_dict();
            let mut mode = None;

            if options.contains("presentation") {
                mode = Some(WindowRunMode::Presentation);
            } else if options.contains("fullscreen") {
                mode = Some(WindowRunMode::Fullscreen);
            }

            let page_index = options.lookup::<i32>("page-index").unwrap();
            let page_label = options.lookup::<String>("page-label").unwrap();
            let named_dest = options.lookup::<String>("named-dest").unwrap();
            let files = options
                .lookup::<Vec<OsString>>(glib::OPTION_REMAINING)
                .unwrap();

            let mut dest = None;

            if let Some(page_label) = page_label {
                dest = Some(LinkDest::new_page_label(&page_label));
            } else if let Some(page_index) = page_index {
                dest = Some(LinkDest::new_page(i32::max(0, page_index - 1)));
            } else if let Some(named_dest) = named_dest {
                dest = Some(LinkDest::new_named(&named_dest));
            }

            match files {
                None => self.open_start_view(),
                Some(files) => {
                    for arg in files {
                        let f = arg.to_string_lossy();

                        if let Some(uri_dest) = self.parse_dest(&f) {
                            dest = Some(uri_dest);
                        }

                        let f = gio::File::for_commandline_arg(arg);
                        self.open_file_at_dest(&f, dest.as_ref(), mode);
                    }
                }
            }

            0.into()
        }

        fn open(&self, files: &[gio::File], _hint: &str) {
            for f in files {
                self.open_file_at_dest(f, None, None);
            }
        }
    }

    impl PpsApplication {
        fn parse_dest(&self, uri: &str) -> Option<papers_document::LinkDest> {
            let Ok((_, _, _, _, _, _, Some(frag))) = glib::Uri::split(uri, glib::UriFlags::ENCODED)
            else {
                return None;
            };

            // We use the standard specified by RFC8118 for all document types.
            match frag.rsplit_once('=') {
                Some(("page", page)) => {
                    if let Ok(n) = page.parse::<u32>()
                        && n > 0
                    {
                        return Some(LinkDest::new_page((n - 1) as i32));
                    }
                }
                Some(("nameddest", named_dest)) => return Some(LinkDest::new_named(named_dest)),
                None => return Some(LinkDest::new_page_label(&frag)),
                _ => (),
            }

            None
        }

        fn open_start_view(&self) {
            PpsWindow::default().present();
        }

        fn open_file_at_dest(
            &self,
            file: &gio::File,
            dest: Option<&papers_document::LinkDest>,
            mode: Option<WindowRunMode>,
        ) {
            let obj = self.obj();
            let mut n_window = 0;
            let mut window = None;
            let uri = file.uri();

            for w in obj
                .windows()
                .into_iter()
                .filter_map(|w| w.downcast::<PpsWindow>().ok())
            {
                if w.is_empty() || w.uri().is_some_and(|u| u == uri) {
                    window = Some(w.clone());
                }

                n_window += 1;
            }

            if n_window != 0 && window.is_none() {
                // There are windows, but they hold a different document.
                // Since we don't have security between documents, then
                // spawn a new process! See:
                // https://gitlab.gnome.org/GNOME/papers/-/issues/104
                spawn(Some(file), dest, mode);
                return;
            }

            let window = window.unwrap_or_default();

            // We need to load uri before showing the window, so
            // we can restore window size without flickering
            window.open(file, dest, mode);
            window.present();
        }

        fn show_about(&self) {
            // Development releases with anything but digits need to use ~
            // as the separator in AppStream for correct sorting:
            // https://handbook.gnome.org/maintainers/making-a-release.html
            let appstream_version = VERSION
                .to_owned()
                .replace(".alpha", "~alpha")
                .replace(".beta", "~beta")
                .replace(".rc", "~rc");
            let about = adw::AboutDialog::from_appdata(
                "/org/gnome/papers/metainfo.xml",
                Some(&appstream_version),
            );

            about.set_copyright(&gettext("© 1996–2025 The Papers authors"));
            about.set_translator_credits(&gettext("translator-credits"));

            let adw_version = format!(
                "{}.{}.{}",
                adw::major_version(),
                adw::minor_version(),
                adw::micro_version()
            );

            let gtk_version = format!(
                "{}.{}.{}",
                gtk::major_version(),
                gtk::minor_version(),
                gtk::micro_version()
            );

            const GIT_COMMIT_ID: &str = git_version!(fallback = VERSION);

            let packaging_format = if std::env::var("FLATPAK_ID").is_ok() {
                "Flatpak"
            } else if std::env::var("SNAP_NAME").is_ok() {
                "Snap"
            } else {
                "System"
            };

            let distribution = if let Ok(os_release) = std::fs::read_to_string("/etc/os-release") {
                os_release
                    .lines()
                    .find(|line| line.starts_with("PRETTY_NAME="))
                    .and_then(|line| line.split_once('='))
                    .map(|(_, name)| name.trim_matches('"'))
                    .unwrap_or("Unknown")
                    .to_string()
            } else {
                "Unknown".to_string()
            };

            let desktop_env =
                std::env::var("XDG_CURRENT_DESKTOP").unwrap_or_else(|_| "Unknown".to_string());

            let mut backend_info_str = String::from("None");
            if let Some(window) = self.obj().active_window().and_downcast::<PpsWindow>()
                && let Some(document) = window.document()
            {
                let mut info = papers_document::DocumentBackendInfo::new();
                if document.is_backend_info(&mut info)
                    && let (Some(name), Some(version)) = (info.name(), info.version())
                {
                    backend_info_str = format!("{name} {version}");
                }
            }

            let debug_info = format!(
                "Document Viewer ({GIT_COMMIT_ID})\n\n\
                *Distribution: {distribution}\n\
                *Desktop: {desktop_env}\n\
                *Packaging: {packaging_format}\n\
                *Backend: {backend_info_str}\n\
                *GTK: {gtk_version}\n\
                *Libadwaita: {adw_version}\n",
            );

            about.set_debug_info(&debug_info);

            about.set_developers(&[
                "Martin Kretzschmar <m_kretzschmar@gmx.net>",
                "Jonathan Blandford <jrb@gnome.org>",
                "Marco Pesenti Gritti <marco@gnome.org>",
                "Nickolay V. Shmyrev <nshmyrev@yandex.ru>",
                "Bryan Clark <clarkbw@gnome.org>",
                "Carlos Garcia Campos <carlosgc@gnome.org>",
                "Wouter Bolsterlee <wbolster@gnome.org>",
                "Christian Persch <chpe\u{0040}src.gnome.org>",
                "Germán Poo-Caamaño <gpoo\u{0040}gnome.org>",
                "Qiu Wenbo <qiuwenbo\u{0040}gnome.org>",
                "Pablo Correa Gómez <ablocorrea\u{0040}hotmail.com>",
                "Markus Göllnitz https://bewares.it/",
            ]);

            about.set_documenters(&[
                "Nickolay V. Shmyrev <nshmyrev@yandex.ru>",
                "Phil Bull <philbull@gmail.com>",
                "Tiffany Antpolski <tiffany.antopolski@gmail.com>",
            ]);

            // Force set the version for the development release
            about.set_version(VERSION);

            about.present(self.obj().active_window().as_ref());
        }

        fn show_help(&self) {
            let context = self
                .obj()
                .active_window()
                .map(|w| gtk::prelude::WidgetExt::display(&w).app_launch_context());
            glib::spawn_future_local(async move {
                if let Err(e) =
                    gio::AppInfo::launch_default_for_uri_future("help:papers", context.as_ref())
                        .await
                {
                    log::error!("Failed to launch help: {}", e.message());
                }
            });
        }

        fn setup_actions(&self) {
            let actions = [
                gio::ActionEntryBuilder::new("about")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            obj.show_about();
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("help")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| obj.show_help()
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("quit")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            /*
                             * Closing an app means closing its foreground tasks.
                             * Our windows are the only top-level entities holding the
                             * application.
                             * Background tasks such as storing a file or printing it,
                             * can independently hold the app until they are finished.
                             * Such a task should not be terminated here.
                             */
                            for window in &obj.obj().windows() {
                                window.close()
                            }
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("new")
                    .activate(|_, _, _| {
                        // spawn an empty window
                        spawn(None, None, None);
                    })
                    .build(),
            ];

            let obj = self.obj();

            obj.add_action_entries(actions);

            obj.set_accels_for_action("app.help", &["F1"]);
            obj.set_accels_for_action("app.new", &["<Ctrl>N"]);
            obj.set_accels_for_action("app.quit", &["<Ctrl>Q"]);

            obj.set_accels_for_action("win.open", &["<Ctrl>O"]);
            obj.set_accels_for_action("win.close", &["<Ctrl>W"]);
            obj.set_accels_for_action("win.fullscreen", &["F11"]);
            obj.set_accels_for_action("win.night-mode", &["<Ctrl>I"]);
            obj.set_accels_for_action("win.presentation", &["F5", "<Shift>F5"]);
        }

        fn setup_command_line(&self) {
            use glib::{OptionArg, OptionFlags};

            let obj = self.obj();

            obj.set_option_context_parameter_string(Some(&gettext("Document Viewer")));

            obj.add_main_option(
                "page-label",
                b'p'.into(),
                OptionFlags::NONE,
                OptionArg::String,
                &gettext("The page label of the document to display."),
                Some(&gettext("PAGE")),
            );

            obj.add_main_option(
                "page-index",
                b'i'.into(),
                OptionFlags::NONE,
                OptionArg::Int,
                &gettext("The page number of the document to display."),
                Some(&gettext("NUMBER")),
            );

            obj.add_main_option(
                "named-dest",
                b'n'.into(),
                OptionFlags::NONE,
                OptionArg::String,
                &gettext("Named destination to display."),
                Some(&gettext("DEST")),
            );

            obj.add_main_option(
                "fullscreen",
                b'f'.into(),
                OptionFlags::NONE,
                OptionArg::None,
                &gettext("Run papers in fullscreen mode."),
                None,
            );

            obj.add_main_option(
                "presentation",
                b's'.into(),
                OptionFlags::NONE,
                OptionArg::None,
                &gettext("Run papers in presentation mode."),
                None,
            );

            obj.add_main_option(
                "version",
                0.into(),
                OptionFlags::NONE,
                OptionArg::None,
                &gettext("Show the version of the program."),
                None,
            );

            obj.add_main_option(
                glib::OPTION_REMAINING,
                0.into(),
                OptionFlags::NONE,
                OptionArg::FilenameArray,
                "",
                Some(&gettext("[FILE…]")),
            );
        }
    }
}

glib::wrapper! {
    pub struct PpsApplication(ObjectSubclass<imp::PpsApplication>)
        @extends adw::Application, gtk::Application, gio::Application,
        @implements gio::ActionGroup, gio::ActionMap;
}

impl Default for PpsApplication {
    fn default() -> Self {
        Self::new()
    }
}

impl PpsApplication {
    pub fn new() -> Self {
        let flags = gio::ApplicationFlags::HANDLES_COMMAND_LINE
            | gio::ApplicationFlags::NON_UNIQUE
            | gio::ApplicationFlags::HANDLES_OPEN;

        let mut builder = glib::Object::builder()
            .property("application-id", APP_ID)
            .property("flags", flags)
            .property("resource-base-path", "/org/gnome/papers");

        #[cfg(not(target_os = "windows"))]
        {
            builder = builder.property("register-session", true);
        }
        #[cfg(target_os = "windows")]
        {
            builder = builder.property("register-session", false);
        }

        builder.build()
    }
}

pub fn spawn(file: Option<&gio::File>, dest: Option<&LinkDest>, mode: Option<WindowRunMode>) {
    match env::current_exe() {
        Ok(path) => {
            cmd.push_str(&format!(" {}", path.to_string_lossy()));

            if let Some(dest) = dest {
                match dest.dest_type() {
                    LinkDestType::PageLabel => {
                        if let Some(label) = dest.page_label() {
                            cmd.arg(format!("--page-label={}", label));
                        }
                    }
                    LinkDestType::Page
                    | LinkDestType::Xyz
                    | LinkDestType::Fit
                    | LinkDestType::Fith
                    | LinkDestType::Fitv
                    | LinkDestType::Fitr => {
                        cmd.arg(format!("--page-index={}", dest.page() + 1));
                    }
                    LinkDestType::Named => {
                        if let Some(named) = dest.named_dest() {
                            cmd.arg(format!("--named-dest={}", named));
                        }
                    }
                    _ => (),
                }
            }

            match mode {
                Some(WindowRunMode::Fullscreen) => {
                    cmd.arg("-f");
                }
                Some(WindowRunMode::Presentation) => {
                    cmd.arg("-s");
                }
                _ => (),
            }

            if let Some(file) = file {
                cmd.arg(file.uri());
            }

            if let Err(e) = cmd.spawn() {
                glib::g_critical!("", "Failed to spawn new papers process: {}", e);
            }
        }
        Err(e) => glib::g_critical!("", "Failed to find current executable: {}", e),
    }
}
