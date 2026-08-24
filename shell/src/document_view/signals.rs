use super::*;

use crate::document_view::enums::AnnotationColor;
use papers_document::{AnnotationTextMarkupType, DocumentPermissions};
use papers_view::AnnotationEditingState;
use papers_view::AnnotationTool;

#[gtk::template_callbacks]
impl imp::PpsDocumentView {
    // default settings
    #[template_callback]
    pub(crate) fn doc_restrictions_changed(&self) {
        let Some(document) = self.document() else {
            self.set_action_enabled("save", false);
            self.set_action_enabled("save-as", false);
            self.set_action_enabled("print", false);
            return;
        };

        let mut ok_to_print = true;
        let mut ok_to_copy = true;

        let info = document.info();
        let override_restrictions = self.settings.boolean(GS_OVERRIDE_RESTRICTIONS);

        if let Some(permissions) = info.and_then(|info| info.permissions())
            && !override_restrictions
        {
            ok_to_print = permissions.contains(DocumentPermissions::OK_TO_PRINT);
            ok_to_copy = permissions.contains(DocumentPermissions::OK_TO_COPY);
        }

        if !papers_view::PrintOperation::exists_for_document(&document) {
            ok_to_print = false;
        }

        if let Some(settings) = self.lockdown_settings() {
            if settings.boolean(GS_LOCKDOWN_SAVE) {
                ok_to_copy = false;
            }

            if settings.boolean(GS_LOCKDOWN_PRINT) {
                ok_to_print = false;
            }
        }

        self.set_action_enabled("save", ok_to_copy);
        self.set_action_enabled("save-as", ok_to_copy);
        self.set_action_enabled("print", ok_to_print);
    }

    #[template_callback]
    pub(super) fn page_cache_size_changed(&self, _: &str, settings: &gio::Settings) {
        self.view
            .set_page_cache_size(settings.uint("page-cache-size") as usize * 1024 * 1024);
    }

    #[template_callback]
    pub(super) fn allow_links_change_zoom_changed(&self, _: &str, settings: &gio::Settings) {
        self.view
            .set_allow_links_change_zoom(settings.boolean("allow-links-change-zoom"));
    }

    #[template_callback]
    pub(super) fn annotation_editing_state_changed(&self) {
        let state = self.model.annotation_editing_state();
        self.update_edit_toolbar_visibility(
            state.contains(AnnotationEditingState::INSERT_TEXT)
                || state.contains(AnnotationEditingState::INK),
        );
    }

    #[template_callback]
    fn button_pressed(&self, _: i32, _: f64, _: f64, controller: &gtk::GestureClick) {
        let Some(event) = controller.current_event() else {
            return;
        };

        if event.event_type() == gdk::EventType::TouchBegin {
            return;
        }

        const MOUSE_BACK_BUTTON: u32 = 8;
        const MOUSE_FORWARD_BUTTON: u32 = 9;

        match event
            .downcast::<gdk::ButtonEvent>()
            .map(|event| event.button())
        {
            Ok(MOUSE_BACK_BUTTON) => {
                let old_page = self.model.page();

                if old_page >= 0 {
                    self.history.add_page(old_page);
                }
                self.history.go_back();
            }
            Ok(MOUSE_FORWARD_BUTTON) => {
                self.history.go_forward();
            }
            _ => (),
        }
    }

    #[template_callback]
    fn scroll_child_history(&self, scroll: gtk::ScrollType, horizontal: bool) -> bool {
        let Some(document) = self.document() else {
            return false;
        };

        if self.model.is_continuous() && !horizontal {
            let new_page = match scroll {
                gtk::ScrollType::Start => 0,
                gtk::ScrollType::End => document.n_pages() - 1,
                _ => return false,
            };

            let old_page = self.model.page();

            self.history.add_page(old_page);
            self.history.add_page(new_page);
        }

        true
    }

    // misc
    #[template_callback]
    fn link_activated(&self, link: &papers_document::Link) {
        self.view.handle_link(link);
    }

    #[template_callback]
    fn history_changed(&self, history: &History) {
        self.set_action_enabled("go-back-history", history.can_go_back());
        self.set_action_enabled("go-forward-history", history.can_go_forward());
    }

    #[template_callback]
    fn print_cancel_alert_response(&self, response: &str) {
        match response {
            "close-later" => {
                if self.print_queue.borrow().is_empty() {
                    if let Some(win) = self.parent_window() {
                        win.destroy();
                    }
                } else {
                    self.close_after_print.set(true);
                }
            }
            "force-close" => {
                self.close_after_print.set(true);
                if !self.print_queue.borrow().is_empty() {
                    self.obj().set_sensitive(false);
                    self.print_cancel();
                }
            }
            _ => self.close_after_print.set(false),
        }
    }

    // sidebar
    #[template_callback]
    pub(crate) fn sidebar_navigate_to_view(&self) {
        if self.split_view.is_collapsed() {
            self.split_view.set_show_sidebar(false);
            self.view.grab_focus();
        }
    }

    #[template_callback]
    fn sidebar_visibility_changed(&self) {
        let settings = self.default_settings.get();
        let show_sidebar = self.split_view.shows_sidebar();

        self.document_action_group
            .change_action_state("show-sidebar", &show_sidebar.into());

        if self
            .sidebar_stack
            .visible_child()
            .is_some_and(|child| child != *self.find_sidebar)
            && !self.split_view.is_collapsed()
        {
            let _ = settings.set_boolean("show-sidebar", show_sidebar);
        }
    }

    #[template_callback]
    fn sidebar_collapsed_changed(&self) {
        if self.split_view.is_collapsed() {
            self.sidebar_was_open_before_collapsed
                .set(self.split_view.shows_sidebar() && self.sidebar_was_open_before_find.get());
            self.split_view.set_show_sidebar(false);
        } else {
            self.split_view
                .set_show_sidebar(self.sidebar_was_open_before_collapsed.get());
        }
    }

    #[template_callback]
    fn sidebar_layers_visibility_changed(&self) {
        self.view.reload();
    }

    #[template_callback]
    fn sidebar_annots_annot_activated(&self, annot: &papers_document::Annotation) {
        self.view.focus_annotation(annot)
    }

    // view
    fn launch_external_uri(&self, action: &LinkAction) {
        let context = self.obj().display().app_launch_context();
        let uri = action.uri().unwrap();
        let file = gio::File::for_uri(&uri);

        let uri = if file.uri_scheme().is_some() {
            uri.to_string()
        } else if uri.starts_with("www.") {
            // Not a valid uri, assume https if it starts with www
            format!("https://{uri}")
        } else {
            let path = self
                .file()
                .and_then(|f| f.path())
                .unwrap()
                .with_file_name(uri);

            glib::filename_to_uri(path, None).unwrap().to_string()
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

    fn launch_action(&self, action: &LinkAction) {
        let Some(filename) = action.filename() else {
            return;
        };

        let uri = if glib::Uri::is_valid(&filename, glib::UriFlags::NONE).is_ok() {
            filename.to_string()
        } else if PathBuf::from(&filename).is_absolute() {
            glib::filename_to_uri(&filename, None).unwrap().to_string()
        } else {
            let file = self.file().and_then(|f| f.parent()).unwrap();
            let target = file.resolve_relative_path(&filename);
            target.uri().to_string()
        };

        let is_pdf = papers_document::file_get_mime_type(&uri, false)
            .ok()
            .map(|content_type| content_type.eq_ignore_ascii_case("application/pdf"))
            .unwrap_or_default();

        if !is_pdf {
            self.warning_message(
                &formatx!(
                    gettext("Security alert: this document has been prevented from opening the file “{}”"),
                    filename,
                )
                .expect("Wrong format in translated string"),
            );
            return;
        }

        // The launch action should not reference the file itself. If it
        // does, simply ignore it
        let file = self.file().unwrap();
        if file.uri() != uri {
            application::spawn(
                Some(&gio::File::for_uri(&uri)),
                action.dest().as_ref(),
                Some(self.mode.get()),
            );
        }
    }

    fn open_remote_link(&self, action: &LinkAction) {
        let Some(path) = self.file().and_then(|f| f.path()) else {
            return;
        };
        let Some(filename) = action.filename() else {
            return;
        };
        let target = path.with_file_name(filename);

        // The goto-remote action should not reference the file itself. If it
        // does, simply ignore it. Ideally, we would not launch a new instance
        // but open a new tab or a new window, but that's not possible until
        // https://gitlab.gnome.org/GNOME/papers/-/issues/104 is fixed
        if target != path {
            application::spawn(
                Some(&gio::File::for_path(target)),
                action.dest().as_ref(),
                Some(WindowRunMode::Normal),
            );
        }
    }

    fn do_action_named(&self, action: &papers_document::LinkAction) {
        let Some(name) = action.name() else { return };
        let obj = self.obj();

        match name.as_str() {
            _ if name.eq_ignore_ascii_case("FirstPage") => {
                self.document_action_group
                    .activate_action("go-first-page", None);
            }
            _ if name.eq_ignore_ascii_case("PrevPage") => self
                .document_action_group
                .activate_action("go-previous-page", None),
            _ if name.eq_ignore_ascii_case("NextPage") => self
                .document_action_group
                .activate_action("go-next-page", None),
            _ if name.eq_ignore_ascii_case("LastPage") => self
                .document_action_group
                .activate_action("go-last-page", None),
            _ if name.eq_ignore_ascii_case("GoToPage") => self
                .document_action_group
                .activate_action("select-page", None),
            _ if name.eq_ignore_ascii_case("Find") => {
                let _ = WidgetExt::activate_action(obj.as_ref(), "doc.find", None);
            }
            _ if name.eq_ignore_ascii_case("Close") => {
                if let Some(win) = obj.root().and_downcast::<gtk::Window>() {
                    win.close();
                } else if let Some(win) = obj.native().and_downcast::<gtk::Window>() {
                    win.close();
                }
            }
            _ if name.eq_ignore_ascii_case("Print") => {
                self.document_action_group.activate_action("print", None)
            }
            _ if name.eq_ignore_ascii_case("Save") => {
                self.document_action_group.activate_action("save", None)
            }
            _ if name.eq_ignore_ascii_case("SaveAs") => {
                self.document_action_group.activate_action("save-as", None)
            }
            _ => {
                glib::g_warning!(
                    "",
                    "Unimplemented named action: {}, please post a bug report in Document Viewer Gitlab (https://gitlab.gnome.org/GNOME/papers/issues) with a testcase.",
                    name
                );
            }
        }
    }

    fn reset_form(&self, action: &papers_document::LinkAction) {
        if let Ok(document) = self.document().and_dynamic_cast::<DocumentForms>() {
            document.reset_form(action);
            self.view.reload();
        }
    }

    #[template_callback]
    pub(super) fn view_external_link(&self, action: &papers_document::LinkAction) {
        match action.action_type() {
            LinkActionType::GotoDest => {
                if let Some(dest) = action.dest() {
                    self.open_copy_at_dest(Some(&dest));
                }
            }
            LinkActionType::ExternalUri => self.launch_external_uri(action),
            LinkActionType::Launch => self.launch_action(action),
            LinkActionType::GotoRemote => self.open_remote_link(action),
            LinkActionType::Named => self.do_action_named(action),
            LinkActionType::ResetForm => self.reset_form(action),
            _ => panic!(),
        }
    }

    fn find_link_in_outlines(outlines: &Outlines, link: &Link) -> Option<glib::GString> {
        let mut link_title = None;

        if let Some(outlines_link) = outlines.link() {
            let a = link.action();
            let b = outlines_link.action();

            if a.is_some() && a == b {
                link_title = link.title();
            }
        }

        if link_title.is_some() {
            return link_title;
        }

        for outlines in outlines.children()?.iter::<Outlines>() {
            let outlines = outlines.unwrap();

            link_title = Self::find_link_in_outlines(&outlines, link);

            if link_title.is_some() {
                return link_title;
            }
        }

        None
    }

    fn find_title_for_link(&self, link: &Link) -> Option<glib::GString> {
        let document = self.document().and_dynamic_cast::<DocumentLinks>().ok()?;
        let model = self.sidebar_links.model()?;

        if !document.has_document_links() {
            return None;
        }

        for outlines in model.iter::<Outlines>() {
            let outlines = outlines.unwrap();

            let link_title = Self::find_link_in_outlines(&outlines, link);

            if link_title.is_some() {
                return link_title;
            }
        }

        None
    }

    #[template_callback]
    fn view_handle_link(
        &self,
        link: &papers_document::Link,
        backlink: Option<&papers_document::Link>,
    ) {
        let mut new_link = None;

        if link.title().is_none() {
            let title = self.find_title_for_link(link);
            let Some(action) = link.action() else { return };

            new_link = if let Some(title) = title {
                Some(papers_document::Link::new(Some(title.as_str()), &action))
            } else {
                let Some(dest) = action.dest() else { return };
                let Some(page_label) = self
                    .document()
                    .and_dynamic_cast::<DocumentLinks>()
                    .ok()
                    .and_then(|d| d.dest_page_label(&dest))
                else {
                    return;
                };

                Some(papers_document::Link::new(
                    Some(
                        &formatx!(gettext("Page {}"), page_label)
                            .expect("Wrong format in translated string"),
                    ),
                    &action,
                ))
            }
        }

        if let Some(backlink) = backlink {
            self.history.add_link(backlink);
        }

        self.history.add_link(new_link.as_ref().unwrap_or(link));
    }

    fn view_menu_link_popup(&self, link: Option<papers_document::Link>) {
        let mut show_external = false;
        let mut show_email = false;
        let mut show_internal = false;

        self.link.take();

        if let Some(link) = link {
            self.link.replace(Some(link.clone()));

            if let Some(action) = link.action() {
                match action.action_type() {
                    LinkActionType::GotoDest | LinkActionType::GotoRemote => show_internal = true,
                    LinkActionType::ExternalUri | LinkActionType::Launch => {
                        if let Some(uri) = action.uri() {
                            if uri.starts_with("mailto:") {
                                show_email = true;
                            } else {
                                show_external = true;
                            }
                        } else {
                            show_external = true;
                        }
                    }
                    _ => (),
                }
            }
        }

        self.set_action_enabled("open-link", show_external);
        self.set_action_enabled("send-email", show_email);
        self.set_action_enabled("copy-link-address", show_external);
        self.set_action_enabled("copy-email-address", show_email);
        self.set_action_enabled("go-to-link", show_internal);
        self.set_action_enabled("open-link-new-window", show_internal);
    }

    fn view_menu_image_popup(&self, image: Option<papers_document::Image>) {
        let show_image = image.is_some();

        self.image.replace(image);

        self.set_action_enabled("save-image", show_image);
        self.set_action_enabled("copy-image", show_image);
    }

    pub(super) fn view_menu_annot_popup(&self, annot: Option<&papers_document::Annotation>) {
        let mut show_annot_props = false;
        let mut is_annot_textmarkup = false;
        let mut show_attachment = false;
        let mut markup_type = "none";
        let has_selection = self.view.has_selection();

        self.annot.replace(annot.cloned());

        if let Some(annot) = annot {
            show_annot_props = annot.is::<papers_document::AnnotationMarkup>();
            is_annot_textmarkup = annot.is::<papers_document::AnnotationTextMarkup>();

            let attachment = annot
                .dynamic_cast_ref::<papers_document::AnnotationAttachment>()
                .and_then(|annot| annot.attachment());

            show_attachment = attachment.is_some();

            self.attachment.replace(attachment);
        }

        let can_remove_annots = self
            .document()
            .and_then(|d| d.dynamic_cast::<DocumentAnnotations>().ok())
            .map(|d| d.can_remove_annotation())
            .unwrap_or_default()
            && annot.is_some();

        self.set_action_enabled("edit-annot", show_annot_props);
        self.set_action_enabled("annot-properties", show_annot_props);
        self.set_action_enabled(
            "annot-textmarkup-type",
            is_annot_textmarkup || has_selection,
        );
        self.set_action_enabled("remove-annot", can_remove_annots);
        self.set_action_enabled("open-attachment", show_attachment);
        self.set_action_enabled("save-attachment", show_attachment);

        if is_annot_textmarkup {
            markup_type = match annot
                .unwrap()
                .dynamic_cast_ref::<AnnotationTextMarkup>()
                .unwrap()
                .markup_type()
            {
                AnnotationTextMarkupType::Highlight => "highlight",
                AnnotationTextMarkupType::Squiggly => "squiggly",
                AnnotationTextMarkupType::StrikeOut => "strikethrough",
                AnnotationTextMarkupType::Underline => "underline",
                _ => panic!("unknown markup type"),
            };
        }

        if let Some(annot) = annot.cloned() {
            let color = AnnotationColor::from(annot.rgba()).to_string();
            self.set_action_state("annot-color", &glib::Variant::from(color.as_str()));
        }
        self.set_action_state("annot-textmarkup-type", &glib::Variant::from(markup_type));
    }

    #[template_callback]
    fn view_menu_popup(&self, items: glib::Pointer, x: f64, y: f64) {
        let mut has_link = false;
        let mut has_image = false;
        let mut has_annot = false;

        let items =
            unsafe { glib::List::<glib::Object>::from_glib_none(items as *const glib::ffi::GList) };

        for o in items {
            if let Some(link) = o.downcast_ref::<papers_document::Link>() {
                self.view_menu_link_popup(Some(link.clone()));
                has_link = true;
            } else if let Some(image) = o.downcast_ref::<papers_document::Image>() {
                self.view_menu_image_popup(Some(image.clone()));
                has_image = true;
            } else if let Some(annot) = o.downcast_ref::<papers_document::Annotation>() {
                self.view_menu_annot_popup(Some(annot));
                has_annot = true;
            }
        }

        if !has_link {
            self.view_menu_link_popup(None);
        }

        if !has_image {
            self.view_menu_image_popup(None);
        }

        if !has_annot {
            self.view_menu_annot_popup(None);
        }

        if self
            .view
            .compute_point(
                &self.view_popup.get(),
                &graphene::Point::new(x as f32, y as f32),
            )
            .is_none()
        {
            glib::g_warning!("", "Out of scope point");
        }

        self.view_popup
            .set_pointing_to(Some(&gdk::Rectangle::new(x as i32, y as i32, 1, 1)));
        self.view_popup.popup();
    }

    #[template_callback]
    fn view_popup_closed(&self) {
        /* clear these during next tick as these values may be used for the actions of the popup menu */
        glib::idle_add_local_once(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            move || {
                obj.annot.replace(None);
                obj.link.replace(None);
                obj.image.replace(None);
            }
        ));
    }

    #[template_callback]
    fn view_selection_changed(&self) {
        let has_selection = self.view.has_selection();
        let can_annotate = self
            .document()
            .and_dynamic_cast::<DocumentAnnotations>()
            .map(|d| d.can_add_annotation())
            .unwrap_or_default();

        self.set_action_enabled("copy", has_selection);
        self.set_action_enabled("annot-textmarkup-type", can_annotate);
    }

    #[template_callback]
    fn scroll_history(&self, scroll: gtk::ScrollType) -> bool {
        let Some(document) = self.document() else {
            return false;
        };
        let old_page = self.model.page();

        let new_page = match scroll {
            gtk::ScrollType::Start => 0,
            gtk::ScrollType::End => document.n_pages() - 1,
            _ => return false,
        };

        self.history.add_page(old_page);
        self.history.add_page(new_page);

        true
    }

    #[template_callback]
    fn view_layers_changed(&self) {
        self.sidebar_layers.update_visibility();
    }

    #[template_callback]
    fn view_caret_cursor_moved(&self, page: i32, offset: i32) {
        if let Some(metadata) = self.metadata() {
            let (page, offset) = (page as u32, offset as u32);

            let caret_position = Variant::from((page, offset)).print(false);

            metadata.set_string("caret-position", &caret_position);
        }
    }

    // model
    #[template_callback]
    pub fn zoom_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let is_free = model.sizing_mode() == SizingMode::Free;

        self.set_action_enabled("zoom-in", self.view.can_zoom_in());
        self.set_action_enabled("zoom-out", self.view.can_zoom_out());

        if let Some(metadata) = self.metadata()
            && is_free
            && !self.is_empty()
        {
            let zoom = model.scale();
            let dpi = Document::misc_get_widget_dpi(self.obj().as_ref());

            metadata.set_double("zoom", zoom * 72. / dpi);
        }
    }

    #[template_callback]
    pub fn sizing_mode_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let hscrollbar_policy = if model.sizing_mode() == SizingMode::Free {
            gtk::PolicyType::Automatic
        } else {
            gtk::PolicyType::Never
        };

        self.scrolled_window
            .set_hscrollbar_policy(hscrollbar_policy);
        self.scrolled_window
            .set_vscrollbar_policy(gtk::PolicyType::Automatic);

        let Some(action) = self.document_action_group.lookup_action("sizing-mode") else {
            return;
        };

        let state = match model.sizing_mode() {
            SizingMode::FitPage => "fit-page",
            SizingMode::FitWidth => "fit-width",
            SizingMode::Automatic => "automatic",
            SizingMode::Free => "free",
            _ => panic!(),
        };

        let show_zoom_fit_best = self.model.sizing_mode() != SizingMode::Automatic;
        if self.zoom_fit_best_revealer.focus_child().is_some() && !show_zoom_fit_best {
            self.view.grab_focus();
        }
        self.zoom_fit_best_revealer
            .set_reveal_child(show_zoom_fit_best);

        action.change_state(&state.into());

        self.metadata_and_then(|metadata| {
            metadata.set_string("sizing-mode", state);
        });
    }

    #[template_callback]
    pub fn rotation_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let rotation = model.rotation();

        self.metadata_and_then(|metadata| {
            metadata.set_int("rotation", rotation);
        });
    }

    #[template_callback]
    pub fn continuous_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let continuous = model.is_continuous();

        self.set_action_state("continuous", &continuous.into());

        self.metadata_and_then(|metadata| {
            metadata.set_boolean("continuous", continuous);
        });
    }

    #[template_callback]
    pub fn page_layout_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let dual_page = model.page_layout() == PageLayout::Dual;

        self.set_action_state("dual-page", &dual_page.into());

        self.metadata_and_then(|metadata| {
            metadata.set_boolean("dual-page", dual_page);
        });
    }

    #[template_callback]
    pub fn dual_mode_odd_pages_left_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let odd_left = model.is_dual_page_odd_pages_left();

        self.set_action_state("dual-odd-left", &odd_left.into());

        self.metadata_and_then(|metadata| {
            metadata.set_boolean("dual-page-odd-left", odd_left);
        });
    }

    #[template_callback]
    pub fn direction_changed(&self, _: glib::ParamSpec, model: DocumentModel) {
        let rtl = model.is_rtl();

        self.set_action_state("rtl", &rtl.into());

        self.metadata_and_then(|metadata| {
            metadata.set_boolean("rtl", rtl);
        });
    }

    #[template_callback]
    pub fn page_changed(&self, _: i32, new_page: i32) {
        self.metadata_and_then(|metadata| {
            metadata.set_int("page", new_page);
        });
    }

    // Signatures

    #[template_callback]
    pub fn view_signature_details(&self) {
        let properties = PpsPropertiesWindow::new();
        properties.set_visible_page_name(Some("signatures"));
        properties.set_document(self.document());

        AdwDialogExt::present(&properties, Some(self.obj().as_ref()));
    }

    #[template_callback]
    fn signature_rect_too_small(&self, response: &str) {
        if response == "sign" {
            self.certificate_save_as_dialog();
            return;
        }

        self.document_action_group
            .activate_action("digital-signing", None);
    }

    fn show_signature_rect_too_small_warning(&self) {
        self.rect_small_alert.present(Some(self.obj().as_ref()));
    }

    fn calculate_font_size(
        rect: &papers_document::Rectangle,
        text: &str,
        border_size: i32,
    ) -> Option<usize> {
        let width = (rect.x2() - rect.x1()) / 2.0 - 2.0 * border_size as f64;
        let height = rect.y2() - rect.y1() - 2.0 * border_size as f64;
        let chars = text.len();
        let mut current_size = 1;

        // Try to find the biggest font size that will fit in the rect
        loop {
            let chars_per_line = (width / current_size as f64).floor() as usize;
            let lines = (height / current_size as f64).floor() as usize;

            if chars_per_line * lines < chars {
                break;
            }

            current_size += 1;
        }

        if current_size > 1 {
            Some(current_size - 1)
        } else {
            None
        }
    }

    #[template_callback]
    fn view_banner_cancelled(&self) {
        self.view.cancel_signature_rect();
        self.banner.set_title("");
        self.banner.set_revealed(false);
    }

    #[template_callback]
    fn on_signature_rect(&self, page: u32, rect: Option<&papers_document::Rectangle>) {
        self.banner.set_revealed(false);

        let Some(rect) = rect else {
            return;
        };

        let Some(signature) = self.signature.borrow().clone() else {
            return;
        };

        self.signature_page.set(page);
        self.signature_bounding_box.replace(Some(*rect));
        signature.set_rect(rect);

        // Calculate font size for main (right) signature text
        let Some(font_size) = Self::calculate_font_size(
            rect,
            &signature.signature().unwrap_or_default(),
            signature.border_width(),
        ) else {
            self.show_signature_rect_too_small_warning();
            return;
        };

        signature.set_font_size(font_size as i32);

        // Calculate font size for left signature text
        let Some(font_size) = Self::calculate_font_size(
            rect,
            &signature.signature_left().unwrap_or_default(),
            signature.border_width(),
        ) else {
            self.show_signature_rect_too_small_warning();
            return;
        };

        signature.set_left_font_size(font_size as i32);

        self.certificate_save_as_dialog();
    }

    #[template_callback(function, name = "is_pencil")]
    fn is_pencil(a: i32) -> bool {
        a == AnnotationTool::Pencil.into_glib()
    }

    #[template_callback(function, name = "is_highlight")]
    fn is_highlight(a: i32) -> bool {
        a == AnnotationTool::Highlight.into_glib()
    }

    #[template_callback(function, name = "is_eraser")]
    fn is_eraser(a: i32) -> bool {
        a == AnnotationTool::Eraser.into_glib()
    }

    #[template_callback(function, name = "is_text")]
    fn is_text(a: AnnotationEditingState) -> bool {
        a.contains(AnnotationEditingState::INSERT_TEXT)
    }

    #[template_callback(function, name = "not_bool")]
    fn not_bool(a: bool) -> bool {
        !a
    }
}
