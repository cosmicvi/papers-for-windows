use crate::deps::*;

use std::cell::Cell;
use std::path::PathBuf;

use glib::UserDirectory;
use glib::closure_local;
use gtk::TextDirection;

use futures::{FutureExt, future::LocalBoxFuture};
use papers_document::{DocumentAnnotations, DocumentMode, LinkAction, LinkActionType, LinkDest};
use papers_view::JobLoad;
use papers_view::JobPriority;
use papers_view::SizingMode;

use crate::application::spawn;
use crate::config::PROFILE;

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub enum WindowRunMode {
    #[default]
    StartView,
    Normal,
    Fullscreen,
    Presentation,
    LoaderView,
    ErrorView,
    PasswordView,
}

mod imp {
    use super::*;

    #[derive(Default, Debug, CompositeTemplate)]
    #[template(resource = "/org/gnome/papers/ui/window.ui")]
    pub struct PpsWindow {
        // stack and views inside it
        #[template_child]
        pub(super) stack: TemplateChild<adw::ViewStack>,
        #[template_child]
        pub(super) loader_view: TemplateChild<PpsLoaderView>,
        #[template_child]
        pub(super) error_page: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub(super) password_view: TemplateChild<PpsPasswordView>,
        #[template_child]
        pub(super) document_view: TemplateChild<PpsDocumentView>,
        #[template_child]
        pub(super) presentation: TemplateChild<papers_view::ViewPresentation>,
        #[template_child]
        pub(super) settings: TemplateChild<gio::Settings>,
        #[template_child]
        pub(super) default_settings: TemplateChild<gio::Settings>,
        #[template_child]
        pub(super) toast_overlay: TemplateChild<adw::ToastOverlay>,
        #[template_child]
        pub(super) error_alert: TemplateChild<adw::AlertDialog>,

        pub(super) mode: Cell<WindowRunMode>,

        pub(super) display_name: RefCell<String>,
        pub(super) edit_name: RefCell<String>,
        pub(super) metadata: RefCell<Option<papers_view::Metadata>>,

        pub(super) dest: RefCell<Option<papers_document::LinkDest>>,

        pub(super) monitor: RefCell<Option<PpsFileMonitor>>,

        // Loaders
        pub(super) local_path: RefCell<Option<PathBuf>>,
        pub(super) file: RefCell<Option<gio::File>>,

        // Reload
        pub(super) uri_mtime: Cell<i64>,

        // Job
        pub(super) load_job: RefCell<Option<papers_view::JobLoad>>,
        pub(super) load_job_handler: RefCell<Option<glib::SignalHandlerId>>,
        pub(super) reload_job: RefCell<Option<papers_view::JobLoad>>,
        pub(super) reload_job_handler: RefCell<Option<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for PpsWindow {
        const NAME: &'static str = "PpsWindow";
        type Type = super::PpsWindow;
        type ParentType = adw::ApplicationWindow;

        fn class_init(klass: &mut Self::Class) {
            // for drop target support
            gdk::FileList::ensure_type();

            papers_view::ViewPresentation::ensure_type();
            PpsPasswordView::ensure_type();
            PpsLoaderView::ensure_type();

            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(obj: &InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for PpsWindow {
        fn constructed(&self) {
            self.parent_constructed();

            #[allow(clippy::const_is_empty)]
            if !PROFILE.is_empty() {
                self.obj().add_css_class("devel");
            }

            self.setup_actions();

            self.obj()
                .change_action_state("night-mode", &self.settings.boolean("night-mode").into());
        }

        fn dispose(&self) {
            self.default_settings.apply();
            self.clear_local_uri();
        }
    }

    impl WidgetImpl for PpsWindow {}

    impl WindowImpl for PpsWindow {
        fn close_request(&self) -> glib::Propagation {
            if self.document_view.close_handled() == glib::Propagation::Stop {
                return glib::Propagation::Stop;
            }

            if self.check_document_modified_reload() {
                return glib::Propagation::Stop;
            }

            self.parent_close_request()
        }
    }

    impl ApplicationWindowImpl for PpsWindow {}

    impl AdwApplicationWindowImpl for PpsWindow {}

    impl PpsWindow {
        // field getter
        fn load_job(&self) -> Option<JobLoad> {
            self.load_job.borrow().clone()
        }

        fn clear_local_uri(&self) {
            if let Some(path) = self.local_path.take() {
                std::fs::remove_file(path).expect("failed to remove temp file");
            }
        }

        fn clear_load_job(&self) {
            if let Some(job) = self.load_job.take() {
                if !job.is_finished() {
                    job.cancel();
                }

                if let Some(id) = self.load_job_handler.take() {
                    job.disconnect(id);
                }
            }
        }

        fn clear_reload_job(&self) {
            if let Some(job) = self.reload_job.take() {
                if !job.is_finished() {
                    job.cancel();
                }

                if let Some(id) = self.reload_job_handler.take() {
                    job.disconnect(id);
                }
            }
        }

        fn set_mode(&self, mode: WindowRunMode) {
            if self.mode.get() == mode {
                return;
            }

            self.mode.set(mode);

            let stack = self.stack.clone();

            match mode {
                WindowRunMode::Normal => stack.set_visible_child_name("document"),
                WindowRunMode::PasswordView => stack.set_visible_child_name("password"),
                WindowRunMode::StartView => stack.set_visible_child_name("start"),
                WindowRunMode::LoaderView => stack.set_visible_child_name("loader"),
                WindowRunMode::ErrorView => stack.set_visible_child_name("error"),
                WindowRunMode::Presentation => stack.set_visible_child_name("presentation"),
                WindowRunMode::Fullscreen => stack.set_visible_child_name("document"),
            }
        }

        pub(super) fn open_copy(
            &self,
            metadata: Option<&papers_view::Metadata>,
            dest: Option<&LinkDest>,
            display_name: &str,
            edit_name: &str,
        ) {
            let win = super::PpsWindow::new();
            let imp = win.imp();
            let document = self.document_view.model().document().unwrap();

            imp.document_view.set_filenames(display_name, edit_name);
            imp.document_view
                .open_document(&document, metadata, dest, WindowRunMode::Normal);
            imp.set_mode(WindowRunMode::Normal);

            win.set_default_size(self.obj().width(), self.obj().height());
            win.present();
        }

        fn check_document_modified(&self) -> Option<String> {
            let document = self.document_view.model().document()?;

            let forms_modified = document
                .dynamic_cast_ref::<papers_document::DocumentForms>()
                .map(|d| d.document_is_modified())
                .unwrap_or_default();
            let annots_modified = document
                .dynamic_cast_ref::<DocumentAnnotations>()
                .map(|d| d.document_is_modified())
                .unwrap_or_default();

            match (forms_modified, annots_modified) {
                (true, true) => Some(gettext(
                    "Document contains new or modified annotations and form fields that have been filled out.",
                )),
                (true, false) => Some(gettext(
                    "Document contains form fields that have been filled out.",
                )),
                (false, true) => Some(gettext("Document contains new or modified annotations.")),
                (false, false) => None,
            }
        }

        fn check_document_modified_reload(&self) -> bool {
            let Some(secondary_text) = self.check_document_modified() else {
                return false;
            };
            let secondary_text_command =
                gettext("If you reload the document, changes will be permanently lost.");
            let text = gettext("File changed outside Document Viewer. Reload document?");

            let dialog = adw::AlertDialog::builder()
                .body(format!("{secondary_text} {secondary_text_command}"))
                .heading(text)
                .default_response("yes")
                .build();

            dialog.add_responses(&[("no", &gettext("_No")), ("yes", &gettext("_Reload"))]);

            dialog.set_response_appearance("yes", adw::ResponseAppearance::Destructive);

            dialog.connect_response(
                None,
                glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, response| {
                        if response == "yes" {
                            glib::spawn_future_local(async move {
                                obj.reload_document().await;
                            });
                        }
                    }
                ),
            );

            dialog.present(Some(self.obj().as_ref()));

            true
        }

        fn run_presentation(&self) {
            let model = self.document_view.model();

            let Some(document) = model.document() else {
                return;
            };

            if let Some(state) = self.obj().action_state("fullscreen")
                && state.get::<bool>().unwrap_or_default()
            {
                self.obj().change_action_state("fullscreen", &false.into());
            }

            self.obj()
                .lookup_action("fullscreen")
                .and_downcast::<gio::SimpleAction>()
                .unwrap()
                .set_enabled(false);

            let page = model.page();
            let rotation = model.rotation();
            let inverted_colors = model.is_inverted_colors();

            self.presentation.set_document(Some(&document));
            self.presentation.set_current_page(page as u32);
            self.presentation.set_rotation(rotation);
            self.presentation.set_inverted_colors(inverted_colors);

            // FIXME: handle external link
            // FIXME: set metadata
            // FIXME: rotate shortcut

            self.set_mode(WindowRunMode::Presentation);
            self.presentation.grab_focus();

            self.obj().fullscreen();
        }

        fn stop_presentation(&self) {
            let page = self.presentation.current_page();
            let rotation = self.presentation.rotation();

            let model = self.document_view.model();

            model.set_page(page as i32);
            model.set_rotation(rotation as i32);

            self.obj()
                .lookup_action("fullscreen")
                .and_downcast::<gio::SimpleAction>()
                .unwrap()
                .set_enabled(true);

            self.presentation
                .set_document(papers_document::Document::NONE);
            self.set_mode(WindowRunMode::Normal);
            self.obj().unfullscreen();
        }

        // Actions
        fn cmd_presentation(&self) {
            if self.mode.get() == WindowRunMode::Presentation {
                // We don't exit presentation when action is toggled because it
                // conflicts with some remote controls. The behaviour is also
                // consistent with libreoffice and other presentation tools.
                // See https://bugzilla.gnome.org/show_bug.cgi?id=556162
                return;
            }

            self.run_presentation();
        }

        fn cmd_escape(&self) {
            if self.mode.get() == WindowRunMode::Presentation {
                self.stop_presentation();

                // FIXME: update the metadata
                return;
            }

            self.document_view
                .activate_action("doc.escape", None)
                .unwrap();
        }

        fn setup_actions(&self) {
            let actions = [
                gio::ActionEntryBuilder::new("open")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            obj.cmd_file_open();
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("close")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            obj.obj().close();
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("fullscreen")
                    .state(false.into())
                    .change_state(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, action, state| {
                            let enabled = state.and_then(|v| v.get::<bool>()).unwrap();

                            obj.document_view.set_fullscreen_mode(enabled);

                            if enabled {
                                if !obj.obj().is_fullscreen() {
                                    obj.obj().fullscreen();
                                }
                            } else if obj.obj().is_fullscreen() {
                                obj.obj().unfullscreen();
                            }

                            action.set_state(state.unwrap());
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("escape")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            obj.cmd_escape();
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("night-mode")
                    .state(false.into())
                    .change_state(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, action, state| {
                            let state = state.unwrap();
                            let night_mode = state.get::<bool>().unwrap();

                            action.set_state(state);

                            obj.set_night_mode(night_mode);
                        }
                    ))
                    .build(),
                gio::ActionEntryBuilder::new("presentation")
                    .activate(glib::clone!(
                        #[weak(rename_to = obj)]
                        self,
                        move |_, _, _| {
                            obj.cmd_presentation();
                        }
                    ))
                    .build(),
            ];

            self.obj().add_action_entries(actions);
        }

        fn show_error(&self, error: Option<&glib::Error>) {
            self.error_page.set_description(error.map(|e| e.message()));
            self.set_mode(WindowRunMode::ErrorView);
        }

        fn error_message(&self, error: Option<&glib::Error>, msg: &str) {
            let toast = adw::Toast::builder().timeout(20).title(msg).build();

            if let Some(error) = error {
                toast.set_button_label(Some(&gettext("View Details")));

                toast.connect_button_clicked(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_| obj.error_alert.present(Some(obj.obj().as_ref()))
                ));

                self.error_alert.set_heading(Some(msg));
                self.error_alert.set_body(error.message());
            }

            self.toast_overlay.add_toast(toast);
        }

        fn load_remote_failed(&self, error: &glib::Error) {
            self.error_message(
                Some(error),
                &self
                    .local_path
                    .borrow()
                    .as_ref()
                    .unwrap()
                    .display()
                    .to_string(),
            );

            self.clear_local_uri();
            self.uri_mtime.set(0);
        }

        async fn remote_file_copy_ready(
            &self,
            source: &gio::File,
            result: Result<(), glib::Error>,
        ) {
            let Err(e) = result else {
                self.load_job
                    .borrow()
                    .as_ref()
                    .unwrap()
                    .scheduler_push_job(JobPriority::PriorityNone);

                if let Ok(info) = source
                    .query_info_future(
                        gio::FILE_ATTRIBUTE_TIME_MODIFIED,
                        gio::FileQueryInfoFlags::NONE,
                        glib::Priority::DEFAULT,
                    )
                    .await
                {
                    let mtime = info
                        .modification_date_time()
                        .map(|datetime| datetime.to_unix())
                        .unwrap_or_default();

                    self.uri_mtime.set(mtime);
                }

                return;
            };

            if e.matches(gio::IOErrorEnum::NotMounted) {
                let operation = gtk::MountOperation::new(Some(self.obj().as_ref()));

                if let Err(e) = source
                    .mount_enclosing_volume_future(gio::MountMountFlags::NONE, Some(&operation))
                    .await
                {
                    self.load_remote_failed(&e);
                } else {
                    // Volume successfully mounted, try opening the file again
                    self.load_remote_file(source).await;
                }
            } else if e.matches(gio::IOErrorEnum::Cancelled) {
                self.clear_load_job();
                self.clear_local_uri();
            } else {
                self.load_remote_failed(&e);
            }
        }

        fn load_remote_file<'a>(&'a self, file: &'a gio::File) -> LocalBoxFuture<'a, ()> {
            async move {
                if self.local_path.borrow().is_none() {
                    // We'd like to keep extension of source uri since
                    // it helps to resolve some mime types, say cbz.
                    let base_name = self.edit_name.borrow().clone();
                    let template = format!("document.XXXXXX-{base_name}");
                    match papers_document::mkstemp(&template) {
                        Ok((_, temp_file)) => {
                            let file = gio::File::for_path(&temp_file);

                            self.load_job
                                .borrow()
                                .as_ref()
                                .unwrap()
                                .set_uri(&file.uri());

                            self.local_path.replace(Some(temp_file));
                        }
                        Err(e) => {
                            self.error_message(Some(&e), &gettext("Failed to Load Remote File."));
                            return;
                        }
                    }
                }

                let target_file = gio::File::for_path(self.local_path.borrow().as_ref().unwrap());

                self.loader_view.set_uri(file.uri());
                self.set_mode(WindowRunMode::LoaderView);

                let (result, mut stream) = file.copy_future(
                    &target_file,
                    gio::FileCopyFlags::OVERWRITE,
                    glib::Priority::DEFAULT,
                );

                use futures::stream::StreamExt;

                glib::spawn_future_local(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    async move {
                        while let Some((n, total)) = stream.next().await {
                            if total < 0 {
                                return;
                            }

                            obj.loader_view.set_fraction(n as f64 / total as f64);
                        }
                    }
                ));

                let result = result.await;

                self.remote_file_copy_ready(file, result).await;
            }
            .boxed_local()
        }

        fn set_filenames(&self, file: &gio::File) {
            let attributes = [
                gio::FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME.as_str(),
                gio::FILE_ATTRIBUTE_STANDARD_EDIT_NAME.as_str(),
            ]
            .join(",");
            let info = file.query_info(
                &attributes,
                gio::FileQueryInfoFlags::NONE,
                gio::Cancellable::NONE,
            );

            let (display_name, edit_name) = match info {
                Ok(info) => {
                    let display_name = info.display_name().to_string();
                    let edit_name = if info.has_attribute(gio::FILE_ATTRIBUTE_STANDARD_EDIT_NAME) {
                        info.edit_name().to_string()
                    } else {
                        display_name.clone()
                    };
                    (display_name, edit_name)
                }
                Err(e) => {
                    glib::g_warning!("", "Failed to query file info: {}", e.message());

                    let basename = file.basename().unwrap().display().to_string();
                    (basename.clone(), basename)
                }
            };

            self.document_view.set_filenames(&display_name, &edit_name);

            self.display_name.replace(display_name);
            self.edit_name.replace(edit_name);
        }

        pub(super) fn init_metadata_with_default_values(&self, file: &gio::File) {
            if !papers_view::Metadata::is_file_supported(file) {
                return;
            }

            let metadata = papers_view::Metadata::new(file);

            let boolean_properties = [
                "continuous",
                "dual-page",
                "dual-page-odd-left",
                "fullscreen",
                "window-maximized",
            ];

            for property in boolean_properties {
                if !metadata.has_key(property) {
                    metadata.set_boolean(property, self.default_settings.boolean(property));
                }
            }

            if !metadata.has_key("rtl") {
                metadata.set_boolean(
                    "rtl",
                    gtk::Widget::default_direction() == TextDirection::Rtl,
                );
            }

            if !metadata.has_key("sizing-mode") {
                let enum_class = glib::EnumClass::new::<SizingMode>();
                let mode = self.default_settings.enum_("sizing-mode");
                let mode = enum_class.value(mode).unwrap().nick();

                metadata.set_string("sizing-mode", mode);
            }

            if !metadata.has_key("zoom") {
                metadata.set_double("zoom", self.default_settings.double("zoom"));
            }

            self.metadata.replace(Some(metadata));
        }

        fn reload_local(&self) {
            let Some(file) = self
                .local_path
                .borrow()
                .as_ref()
                .map(gio::File::for_path)
                .or(self.file.borrow().clone())
            else {
                return;
            };

            let uri = file.uri();

            let job = JobLoad::new();
            job.set_uri(&uri);

            let id = job.connect_finished(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                move |job| {
                    if job.is_succeeded().is_err() {
                        obj.clear_reload_job();
                        obj.dest.take();
                        return;
                    }

                    let document = job.loaded_document();

                    obj.document_view.reload_document(document.as_ref());

                    obj.clear_reload_job();
                }
            ));

            job.scheduler_push_job(JobPriority::PriorityNone);
            self.reload_job.replace(Some(job));
            self.reload_job_handler.replace(Some(id));
        }

        async fn copy_and_reload_local(&self, remote: &gio::File) {
            let target = gio::File::for_path(self.local_path.borrow().as_ref().unwrap());

            let (result, _) = remote.copy_future(
                &target,
                gio::FileCopyFlags::OVERWRITE,
                glib::Priority::DEFAULT,
            );

            if let Err(e) = result.await {
                if !e.matches(gio::IOErrorEnum::Cancelled) {
                    self.error_message(Some(&e), &gettext("Failed to Reload Document."));
                }
            } else {
                self.reload_local();
            }
        }

        async fn reload_remote(&self) {
            let Some(remote) = self.file.borrow().clone() else {
                return;
            };

            // Reload the remote uri only if it has changed
            if let Ok(info) = remote
                .query_info_future(
                    gio::FILE_ATTRIBUTE_TIME_MODIFIED,
                    gio::FileQueryInfoFlags::NONE,
                    glib::Priority::DEFAULT,
                )
                .await
                && let Some(time) = info.modification_date_time()
            {
                let mtime = time.to_unix();

                if self.uri_mtime.get() != mtime {
                    // Remote file has changed
                    self.uri_mtime.set(mtime);
                    self.copy_and_reload_local(&remote).await;
                }
            }

            self.reload_local();
        }

        async fn reload_document(&self) {
            self.clear_reload_job();
            self.dest.take();

            if self.local_path.borrow().is_some() {
                self.reload_remote().await;
            } else {
                self.reload_local();
            }
        }

        fn file_changed(&self) {
            if !self.check_document_modified_reload() {
                glib::spawn_future_local(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    async move {
                        obj.reload_document().await;
                    }
                ));
            }
        }

        fn setup_window_size(&self) {
            // HACK: Mutte seems send a configure event of the original size
            // if we set the window size immediately after the dialog is closed
            let window = self.obj().clone();

            glib::timeout_add_local_once(
                std::time::Duration::from_millis(100),
                glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move || {
                        obj.default_settings.delay();
                        obj.default_settings
                            .bind("window-width", &window, "default-width")
                            .build();
                        obj.default_settings
                            .bind("window-height", &window, "default-height")
                            .build();
                        obj.default_settings
                            .bind("window-maximized", &window, "maximized")
                            .build();
                        // When the window is resized, the sidebar may incorrectly grab the focus
                        glib::timeout_add_local_once(
                            std::time::Duration::from_millis(100),
                            glib::clone!(
                                #[weak(rename_to = obj)]
                                obj,
                                move || {
                                    if obj.mode.get() != WindowRunMode::Presentation {
                                        obj.document_view.focus_view()
                                    }
                                }
                            ),
                        );
                    }
                ),
            );
        }

        pub(super) fn open(
            &self,
            file: &impl IsA<gio::File>,
            dest: Option<&papers_document::LinkDest>,
            mode: Option<WindowRunMode>,
        ) {
            self.clear_load_job();
            self.clear_local_uri();

            self.dest.replace(dest.cloned());

            // Create a monitor for the document
            let monitor = PpsFileMonitor::new(&file.uri());
            monitor.connect_closure(
                "changed",
                false,
                closure_local!(
                    #[weak_allow_none(rename_to = obj)]
                    self,
                    move |_: glib::Object| {
                        if let Some(obj) = obj {
                            obj.file_changed();
                        }
                    }
                ),
            );
            self.monitor.replace(Some(monitor));

            // Try to use FUSE-backed files if possible to avoid downloading
            let path = file
                .path()
                .and_then(|p| glib::filename_to_uri(p, None).ok());
            let file_uri = file.uri();
            let uri = path
                .as_ref()
                .map(|gs| gs.as_str())
                .unwrap_or(file_uri.as_str());

            self.set_filenames(file.as_ref());
            self.init_metadata_with_default_values(file.as_ref());

            self.setup_window_size();

            let load_job = papers_view::JobLoad::new();
            load_job.set_uri(uri);

            let dest = dest.cloned();

            let id = load_job.connect_finished(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                #[strong]
                mode,
                move |job| {
                    let uri = obj.obj().uri().unwrap();

                    match job.is_succeeded() {
                        Ok(()) => {
                            let document = job.loaded_document().unwrap();

                            // Handle PDF page mode feature
                            let mut pending_mode = WindowRunMode::Normal;
                            if let Some(info) = document.info()
                                && let Some(page_mode) = info.start_mode()
                                && page_mode == DocumentMode::FullScreen
                            {
                                pending_mode = WindowRunMode::Presentation;
                            }

                            if let Some(m) = mode {
                                pending_mode = m;
                            }

                            obj.document_view.open_document(
                                &document,
                                obj.metadata.borrow().clone().as_ref(),
                                dest.as_ref(),
                                pending_mode,
                            );

                            obj.document_view
                                .set_inverted_colors(obj.settings.boolean("night-mode"));

                            match pending_mode {
                                WindowRunMode::Presentation => obj.run_presentation(),
                                WindowRunMode::Fullscreen => {
                                    WidgetExt::activate_action(
                                        obj.obj().as_ref(),
                                        "win.fullscreen",
                                        None,
                                    )
                                    .unwrap();
                                    obj.set_mode(pending_mode);
                                }
                                _ => obj.set_mode(pending_mode),
                            };

                            gtk::RecentManager::default().add_item(&uri);

                            #[cfg(feature = "with-keyring")]
                            if let Some(password) = job.password() {
                                let flags = job.password_save();
                                glib::spawn_future_local(async move {
                                    if let Err(e) =
                                        crate::keyring::save_password(&uri, &password, flags).await
                                    {
                                        glib::g_warning!("", "Failed to save password: {e}");
                                    }
                                });
                            }

                            obj.clear_load_job();
                        }
                        Err(e) => {
                            glib::spawn_future_local(glib::clone!(
                                #[weak]
                                job,
                                async move {
                                    if e.matches(papers_document::DocumentError::Encrypted) {
                                        obj.set_mode(WindowRunMode::PasswordView);

                                        // First look whether password is in keyring
                                        #[cfg(feature = "with-keyring")]
                                        match crate::keyring::lookup_password(&uri).await {
                                            Ok(Some(password)) => {
                                                if job.password().is_some_and(|p| p == password) {
                                                    // Password in keyring is wrong
                                                    job.set_password(None);
                                                } else {
                                                    job.set_password(Some(&password));
                                                    job.scheduler_push_job(
                                                        JobPriority::PriorityNone,
                                                    );
                                                    return;
                                                }
                                            }
                                            Ok(None) => {}
                                            Err(e) => {
                                                glib::g_warning!(
                                                    "",
                                                    "Failed to lookup password: {e}"
                                                );
                                            }
                                        }

                                        // We need to ask the user for a password
                                        let wrong_password = job.password().is_some();

                                        job.set_password(None);
                                        obj.password_view
                                            .set_filename(obj.display_name.borrow().as_str());
                                        obj.password_view.ask_password(wrong_password);
                                        obj.set_mode(WindowRunMode::PasswordView)
                                    } else {
                                        obj.show_error(Some(&e));

                                        obj.clear_load_job();
                                        obj.clear_local_uri();
                                    }
                                }
                            ));
                        }
                    }
                }
            ));

            self.load_job.replace(Some(load_job.clone()));
            self.load_job_handler.replace(Some(id));

            self.file.replace(Some(file.clone().into()));

            let file = file.clone();

            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                async move {
                    // Check if the file comes from a remote GVFS backend (like Google Drive, SMB, SFTP, etc.)
                    // These usually need to be copied locally to avoid access or seek issues.
                    let is_gvfs = file
                        .uri_scheme()
                        .map(|scheme| {
                            // List of GVFS backends that should be copied locally:
                            // - Network: smb, ftp, sftp, dav/davs, nfs
                            // - Cloud: google-drive, onedrive
                            // - Mobile: afc (ios), mtp (android)
                            // - Other: afp (Apple Filing Protocol)
                            matches!(
                                scheme.as_str(),
                                "afc"
                                    | "afp"
                                    | "dav"
                                    | "davs"
                                    | "ftp"
                                    | "google-drive"
                                    | "mtp"
                                    | "nfs"
                                    | "onedrive"
                                    | "sftp"
                                    | "smb"
                            )
                        })
                        .unwrap_or(false);

                    // If the file doesn't have a normal local path or if it's remote file,
                    // then copy it locally before opening to avoid permission or access problems.
                    if path.is_none() || is_gvfs {
                        obj.load_remote_file(file.as_ref()).await;
                    } else {
                        // File appears to be local, but verify it's actually seekable.
                        // Some FUSE-backed GVFS filesystems may provide paths but still have seekability issues
                        let result = file.read_future(glib::Priority::DEFAULT).await;

                        match result {
                            Ok(stream) => {
                                // Check If the stream doesn’t support seeking at all, then treat it as remote.
                                if !stream.can_seek() {
                                    return obj.load_remote_file(file.as_ref()).await;
                                }

                                // Verify that G_SEEK_END works because
                                // some GVFS backends claim to support seeking, but fail when using G_SEEK_END.
                                let seekable = stream.upcast_ref::<gio::Seekable>();
                                if seekable
                                    .seek(0, glib::SeekType::End, gio::Cancellable::NONE)
                                    .is_err()
                                {
                                    return obj.load_remote_file(file.as_ref()).await;
                                }

                                // If stream is properly seekable, load directly
                                obj.loader_view.set_fraction(-1_f64);
                                obj.set_mode(WindowRunMode::LoaderView);
                                load_job.scheduler_push_job(papers_view::JobPriority::PriorityNone);
                            }
                            Err(_) => {
                                // If it failed to open stream, try copying to local
                                obj.load_remote_file(file.as_ref()).await;
                            }
                        }
                    }
                }
            ));
        }

        fn set_night_mode(&self, night_mode: bool) {
            self.document_view.set_inverted_colors(night_mode);

            let manager = adw::StyleManager::for_display(&WidgetExt::display(&self.obj().clone()));

            let color_scheme = if night_mode {
                adw::ColorScheme::ForceDark
            } else {
                adw::ColorScheme::Default
            };

            manager.set_color_scheme(color_scheme);

            self.settings
                .set_boolean("night-mode", night_mode)
                .expect("failed to save night-mode");
        }

        fn settings_key_for_directory(dir: UserDirectory) -> String {
            match dir {
                UserDirectory::Pictures => "pictures-directory",
                _ => "document-directory",
            }
            .into()
        }

        fn file_dialog_restore_folder(&self, dialog: &gtk::FileDialog, dir: UserDirectory) {
            let settings = self.settings.get();
            let key = Self::settings_key_for_directory(dir);

            let folder = settings.get::<Option<String>>(&key).map(PathBuf::from);
            let folder = folder
                .or_else(|| glib::user_special_dir(dir))
                .unwrap_or_else(glib::home_dir);

            dialog.set_initial_folder(Some(&gio::File::for_path(folder)));
        }

        fn file_dialog_save_folder(&self, file: Option<&gio::File>, dir: UserDirectory) {
            let folder = file.and_then(|f| f.parent());

            // store 'nothing' if the folder is the default one
            let path = folder
                .filter(|f| f.path() != glib::user_special_dir(dir))
                .and_then(|f| f.path())
                .and_then(|path| path.into_os_string().into_string().ok());

            let settings = self.settings.get();
            let key = Self::settings_key_for_directory(dir);

            if let Err(e) = settings.set(&key, path) {
                glib::g_warning!("", "Failed to save folder path: {e}");
            }
        }

        fn cmd_file_open(&self) {
            let dialog = gtk::FileDialog::builder().modal(true).build();

            papers_document::Document::factory_add_filters(&dialog, Document::NONE);

            self.file_dialog_restore_folder(&dialog, UserDirectory::Documents);

            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                async move {
                    let Ok(files) = dialog.open_multiple_future(Some(obj.obj().as_ref())).await
                    else {
                        return;
                    };

                    for f in files.iter::<gio::File>() {
                        let f = f.unwrap();
                        let uri = f.uri();

                        if obj.obj().uri().is_some_and(|u| u != uri) {
                            spawn(Some(&f), None, None);
                        } else {
                            obj.open(&f, None, None);
                        }
                    }

                    if files.n_items() > 0 {
                        let file = files.item(0).and_downcast::<gio::File>();

                        obj.file_dialog_save_folder(file.as_ref(), UserDirectory::Documents);
                    }
                }
            ));
        }
    }

    #[gtk::template_callbacks]
    impl PpsWindow {
        #[template_callback]
        fn window_fullscreened(&self) {
            let obj = self.obj();

            if !obj.is_fullscreen() {
                obj.change_action_state("fullscreen", &false.into());
            }
        }

        #[template_callback]
        fn night_mode_changed(&self) {
            let night_mode = self.settings.boolean("night-mode");

            let current = self
                .obj()
                .action_state("night-mode")
                .unwrap()
                .get::<bool>()
                .unwrap();

            if night_mode != current {
                self.obj()
                    .change_action_state("night-mode", &night_mode.into());
            }
        }

        #[template_callback]
        fn loader_view_cancelled(&self) {
            // FIXME: cancellable
            self.set_mode(WindowRunMode::StartView);
        }

        #[template_callback]
        fn password_view_unlock(&self, password: &str, flags: gio::PasswordSave) {
            debug_assert!(self.load_job().is_some());

            if let Some(load_job) = self.load_job() {
                load_job.set_password(Some(password));
                load_job.set_password_save(flags);
                load_job.scheduler_push_job(JobPriority::PriorityNone);
            }
        }

        #[template_callback]
        fn password_view_cancelled(&self) {
            if self.mode.get() == WindowRunMode::StartView {
                self.clear_load_job();
            }
        }

        fn launch_external_uri(&self, action: &LinkAction) {
            let context = WidgetExt::display(&self.obj().clone()).app_launch_context();
            let uri = action.uri().unwrap();
            let file = gio::File::for_uri(&uri);

            let uri = if file.uri_scheme().is_some() {
                uri.to_string()
            } else if uri.starts_with("www.") {
                // Not a valid uri, assume https if it starts with www
                format!("https://{uri}")
            } else {
                return;
            };

            debug!("Launch external uri: {uri}");

            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                async move {
                    if let Err(e) =
                        gio::AppInfo::launch_default_for_uri_future(&uri, Some(&context)).await
                    {
                        obj.error_message(Some(&e), &gettext("Unable to open external link"));
                    }
                }
            ));
        }

        #[template_callback]
        fn external_link_clicked(&self, action: &LinkAction) {
            if action.action_type() == LinkActionType::ExternalUri {
                self.launch_external_uri(action);
            }
        }

        #[template_callback]
        fn presentation_finished(&self) {
            WidgetExt::activate_action(self.obj().as_ref(), "win.escape", None)
                .expect("Can't activate action win.escape");
        }

        #[template_callback]
        fn drag_data_received(&self, value: glib::BoxedValue) -> bool {
            let Ok(file_list) = value.get_owned::<gdk::FileList>() else {
                return false;
            };

            for file in file_list.files() {
                let uri = file.uri();
                let current_uri = self.file.borrow().as_ref().map(|f| f.uri());

                // Only open the file if we don't have an uri, or if it's
                // different to our current one. Don't reload the current open
                // document!
                if let Some(current_uri) = current_uri {
                    if current_uri != uri {
                        crate::application::spawn(Some(&file), None, None);
                    }
                } else {
                    self.open(&file, None, None);
                }
            }

            true
        }
    }
}

glib::wrapper! {
    pub struct PpsWindow(ObjectSubclass<imp::PpsWindow>)
        @extends gtk::Widget, gtk::Window, gtk::ApplicationWindow, adw::ApplicationWindow,
        @implements gtk::Native, gio::ActionGroup, gio::ActionMap, gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Root, gtk::ShortcutManager;
}

impl PpsWindow {
    pub fn new() -> Self {
        glib::Object::builder()
            .property("application", gio::Application::default())
            .property("show-menubar", false)
            .build()
    }

    pub fn uri(&self) -> Option<String> {
        self.imp().file.borrow().as_ref().map(|f| f.uri().into())
    }

    pub fn open(
        &self,
        file: &gio::File,
        dest: Option<&papers_document::LinkDest>,
        mode: Option<WindowRunMode>,
    ) {
        self.imp().open(file, dest, mode)
    }

    pub fn document(&self) -> Option<papers_document::Document> {
        self.imp().document_view.model().document()
    }

    pub fn is_empty(&self) -> bool {
        self.imp().document_view.is_empty() && self.imp().load_job.borrow().is_none()
    }

    pub fn metadata(&self) -> Option<papers_view::Metadata> {
        self.imp().metadata.borrow().clone()
    }

    pub fn print_range(&self, first_page: i32, last_page: i32) {
        self.imp().document_view.print_range(first_page, last_page);
    }

    pub fn open_copy(
        &self,
        metadata: Option<&papers_view::Metadata>,
        dest: Option<&LinkDest>,
        display_name: &str,
        edit_name: &str,
    ) {
        self.imp()
            .open_copy(metadata, dest, display_name, edit_name);
    }
}

impl Default for PpsWindow {
    fn default() -> Self {
        Self::new()
    }
}
