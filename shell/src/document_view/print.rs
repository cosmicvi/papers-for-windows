use super::*;

const PRINT_SETTINGS_FILE: &str = "print-settings";
const PAGE_SETUP_GROUP: &str = "Page Setup";
const PRINT_SETTINGS_GROUP: &str = "Print Settings";

static DOCUMENT_PRINT_SETTINGS: &[&glib::GStr] = &[
    gtk::PRINT_SETTINGS_REVERSE,
    gtk::PRINT_SETTINGS_NUMBER_UP,
    gtk::PRINT_SETTINGS_SCALE,
    gtk::PRINT_SETTINGS_PRINT_PAGES,
    gtk::PRINT_SETTINGS_PAGE_RANGES,
    gtk::PRINT_SETTINGS_PAGE_SET,
    gtk::PRINT_SETTINGS_OUTPUT_URI,
];

impl imp::PpsDocumentView {
    pub(super) fn print_cancel(&self) {
        for op in self.print_queue.borrow_mut().drain(..) {
            op.cancel();
        }
    }

    pub(super) fn check_print_queue(&self) -> bool {
        let n_jobs = self.print_queue.borrow().len();

        if n_jobs == 0 {
            return false;
        }

        self.print_cancel_alert.present(Some(self.obj().as_ref()));
        true
    }

    fn begin_print(&self) {
        self.update_pending_jobs_message(self.print_queue.borrow().len());
    }

    fn status_changed(&self, op: &papers_view::PrintOperation) {
        let status = op.status();
        let fraction = op.progress();

        if self.message_area().is_none() {
            let job_name = op.job_name().unwrap_or_default();
            // TRANSLATORS: In the context of "the job {} is being printed"
            let text = formatx!(gettext("Printing job “{}”"), job_name)
                .expect("Wrong format in translated string");

            let area = PpsProgressMessageArea::new("document-print-symbolic", &text);
            area.add_button(&gettext("C_ancel"), gtk::ResponseType::Cancel);

            #[allow(deprecated)]
            area.info_bar().connect_response(glib::clone!(
                #[weak]
                area,
                #[weak]
                op,
                move |_, response| {
                    if response == gtk::ResponseType::Cancel {
                        op.cancel();
                    } else {
                        area.set_visible(false);
                    }
                }
            ));

            self.set_message_area(Some(&area));
        }

        let area = self.message_area().unwrap();
        area.set_status(status.unwrap_or_default());
        area.set_fraction(fraction);
    }

    fn update_pending_jobs_message(&self, n_jobs: usize) {
        let Some(area) = self.message_area() else {
            return;
        };

        match n_jobs {
            0 => {
                self.set_message_area(None);
            }
            1 => area.set_secondary_text(gettext("No pending jobs in queue")),
            n => {
                let text = formatx!(
                    ngettext(
                        "{} pending job in queue",
                        "{} pending jobs in queue",
                        n as u32 - 1
                    ),
                    n,
                )
                .expect("Wrong format in translated string");
                area.set_secondary_text(text);
            }
        }
    }

    fn done(&self, op: &papers_view::PrintOperation, result: PrintOperationResult) {
        match result {
            PrintOperationResult::Apply => {
                if let Some(print_settings) = op.print_settings() {
                    self.save_print_settings(&print_settings);
                }

                if op.embeds_page_setup() {
                    let page_setup = op.default_page_setup();
                    self.save_print_page_setup(&page_setup.unwrap())
                }
            }
            PrintOperationResult::Error => self.error_message(
                op.error().err().as_ref(),
                &gettext("Failed to Print Document"),
            ),
            _ => (),
        }

        self.print_queue.borrow_mut().retain(|e| e != op);

        let n_jobs = self.print_queue.borrow().len();
        self.update_pending_jobs_message(n_jobs);

        if self.print_queue.borrow().is_empty() && self.close_after_print.get() {
            glib::idle_add_local_once(glib::clone!(
                #[weak(rename_to = obj)]
                self,
                move || {
                    if let Some(win) = obj.parent_window() {
                        win.destroy();
                    }
                }
            ));
        }
    }

    pub(super) fn print_range(&self, first_page: i32, last_page: i32) {
        let document = self.document().unwrap();

        let Some(operation) = papers_view::PrintOperation::new(&document) else {
            glib::g_warning!("", "Printing is not supported for document");
            return;
        };

        operation.connect_begin_print(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            move |_| {
                obj.begin_print();
            }
        ));

        operation.connect_status_changed(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            move |op| {
                obj.status_changed(op);
            }
        ));

        operation.connect_done(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            move |op, result| {
                obj.done(op, result);
            }
        ));

        let current_page = self.model.page();
        let document_last_page = document.n_pages();

        // load print settings
        let print_settings_file = self.print_settings_keyfile();
        let print_settings = self.print_settings(&print_settings_file);
        let page_setup = self.print_page_setup(&print_settings_file);

        self.load_print_settings_from_metadata(&print_settings);
        self.load_print_page_setup_from_metadata(&page_setup);

        if first_page != 1 || last_page != document_last_page {
            // ranges in GtkPrint are 0 - N
            let range = gtk::PageRange::new(first_page - 1, last_page - 1);

            print_settings.set_print_pages(gtk::PrintPages::Ranges);
            print_settings.set_page_ranges(&[range]);
        }

        let output_basename = PathBuf::from(self.edit_name.borrow().clone())
            .with_extension("")
            .display()
            .to_string();
        print_settings.set(gtk::PRINT_SETTINGS_OUTPUT_BASENAME, Some(&output_basename));

        let window = self.parent_window();
        if let Some(ref win) = window {
            operation.set_job_name(&win.title().unwrap_or_default());
        }
        operation.set_current_page(current_page);
        operation.set_print_settings(&print_settings);
        operation.set_default_page_setup(&page_setup);

        let embed_page_setup = self
            .lockdown_settings
            .borrow()
            .clone()
            .map(|s| !s.boolean(GS_LOCKDOWN_PRINT_SETUP))
            .unwrap_or(true);
        operation.set_embed_page_setup(embed_page_setup);

        self.print_queue.borrow_mut().push_back(operation.clone());

        if let Some(ref win) = window {
            operation.run(win);
        }
    }

    fn print_settings_filename(&self, create: bool) -> PathBuf {
        let dot_dir = glib::user_config_dir().join("papers");

        if create {
            glib::mkdir_with_parents(&dot_dir, 0o700);
        }

        dot_dir.join(PRINT_SETTINGS_FILE)
    }

    fn print_settings_keyfile(&self) -> glib::KeyFile {
        let file = glib::KeyFile::new();
        let filename = self.print_settings_filename(false);
        let flags = glib::KeyFileFlags::KEEP_COMMENTS | glib::KeyFileFlags::KEEP_TRANSLATIONS;

        if let Err(e) = file.load_from_file(filename, flags) {
            // Don't warn if the file simply doesn't exist
            if !e.matches(glib::FileError::Noent) {
                glib::g_warning!("", "{}", e.message());
            }
        }

        file
    }

    fn save_print_settings_file(&self, file: &glib::KeyFile) {
        let filename = self.print_settings_filename(true);
        if let Err(e) = file.save_to_file(filename) {
            glib::g_warning!("", "Failed to save print settings: {}", e.message());
        }
    }

    fn save_print_settings(&self, print_settings: &gtk::PrintSettings) {
        let key_file = self.print_settings_keyfile();
        print_settings.to_key_file(&key_file, Some(PRINT_SETTINGS_GROUP));

        // Always Remove n_copies from global settings
        let _ = key_file.remove_key(PRINT_SETTINGS_GROUP, gtk::PRINT_SETTINGS_N_COPIES);

        // Save print settings that are specific to the document
        for setting in DOCUMENT_PRINT_SETTINGS {
            // Remove it from global settings
            let _ = key_file.remove_key(PRINT_SETTINGS_GROUP, setting);

            if let Some(value) = print_settings.get(setting) {
                self.metadata_and_then(move |metadata| {
                    metadata.set_string(setting, &value);
                });
            }
        }

        self.save_print_settings_file(&key_file);
    }

    fn save_print_page_setup(&self, page_setup: &gtk::PageSetup) {
        let file = self.print_settings_keyfile();

        page_setup.to_key_file(&file, Some(PAGE_SETUP_GROUP));

        // Do not save document settings in global file
        let _ = file.remove_key(PAGE_SETUP_GROUP, "page-setup-orientation");
        let _ = file.remove_key(PAGE_SETUP_GROUP, "page-setup-margin-top");
        let _ = file.remove_key(PAGE_SETUP_GROUP, "page-setup-margin-bottom");
        let _ = file.remove_key(PAGE_SETUP_GROUP, "page-setup-margin-left");
        let _ = file.remove_key(PAGE_SETUP_GROUP, "page-setup-margin-right");

        self.save_print_settings_file(&file);

        // Save page setup options that are specific to the document
        if let Some(metadata) = self.metadata() {
            metadata.set_int(
                "page-setup-orientation",
                page_setup.orientation().into_glib(),
            );
            metadata.set_double(
                "page-setup-margin-top",
                page_setup.top_margin(gtk::Unit::Mm),
            );
            metadata.set_double(
                "page-setup-margin-bottom",
                page_setup.bottom_margin(gtk::Unit::Mm),
            );
            metadata.set_double(
                "page-setup-margin-left",
                page_setup.left_margin(gtk::Unit::Mm),
            );
            metadata.set_double(
                "page-setup-margin-right",
                page_setup.right_margin(gtk::Unit::Mm),
            );
        }
    }

    fn print_settings(&self, key_file: &glib::KeyFile) -> gtk::PrintSettings {
        gtk::PrintSettings::from_key_file(key_file, Some(PRINT_SETTINGS_GROUP)).unwrap_or_default()
    }

    fn print_page_setup(&self, key_file: &glib::KeyFile) -> gtk::PageSetup {
        gtk::PageSetup::from_key_file(key_file, Some(PAGE_SETUP_GROUP)).unwrap_or_default()
    }

    fn load_print_settings_from_metadata(&self, settings: &gtk::PrintSettings) {
        let Some(metadata) = self.metadata() else {
            return;
        };

        // Load print setting that are specific to the document
        for setting in DOCUMENT_PRINT_SETTINGS {
            let key = setting.as_str();

            settings.set(key, metadata.string(key).as_ref().map(|gs| gs.as_str()));
        }
    }

    fn load_print_page_setup_from_metadata(&self, page_setup: &gtk::PageSetup) {
        let paper_size = page_setup.paper_size();

        // Load page setup options that are specific to the document
        let Some(metadata) = self.metadata() else {
            return;
        };

        let orientation = unsafe {
            gtk::PageOrientation::from_glib(
                metadata
                    .int("page-setup-orientation")
                    .unwrap_or(gtk::PageOrientation::Portrait.into_glib()),
            )
        };

        page_setup.set_orientation(orientation);

        page_setup.set_top_margin(
            metadata
                .double("page-setup-margin-top")
                .unwrap_or_else(|| paper_size.default_top_margin(gtk::Unit::Mm)),
            gtk::Unit::Mm,
        );

        page_setup.set_bottom_margin(
            metadata
                .double("page-setup-margin-bottom")
                .unwrap_or_else(|| paper_size.default_bottom_margin(gtk::Unit::Mm)),
            gtk::Unit::Mm,
        );

        page_setup.set_left_margin(
            metadata
                .double("page-setup-margin-left")
                .unwrap_or_else(|| paper_size.default_left_margin(gtk::Unit::Mm)),
            gtk::Unit::Mm,
        );

        page_setup.set_right_margin(
            metadata
                .double("page-setup-margin-right")
                .unwrap_or_else(|| paper_size.default_right_margin(gtk::Unit::Mm)),
            gtk::Unit::Mm,
        );
    }
}
