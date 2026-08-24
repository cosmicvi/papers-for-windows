use super::*;

use crate::document_view::enums::AnnotationColor;
use gtk::gdk::gdk_pixbuf;
use papers_document::{AnnotationTextMarkupType, DocumentImages, DocumentSignatures};
use papers_view::AnnotationTool;
use papers_view::annotations_context::AddAnnotationData;

fn gdk_pixbuf_format_by_extension(uri: &str) -> Option<gdk_pixbuf::PixbufFormat> {
    for format in gdk_pixbuf::Pixbuf::formats()
        .into_iter()
        .filter(|f| !f.is_disabled() && f.is_writable())
    {
        for ext in format.extensions() {
            if uri.ends_with(&*ext) {
                return Some(format);
            }
        }
    }

    None
}

impl imp::PpsDocumentView {
    pub(crate) fn set_action_enabled(&self, name: &str, enabled: bool) {
        let action = self
            .document_action_group
            .lookup_action(name)
            .and_downcast::<gio::SimpleAction>()
            .unwrap_or_else(|| panic!("there is no action named {name}"));

        action.set_enabled(enabled);
    }

    pub(crate) fn set_action_state(&self, name: &str, state: &glib::Variant) {
        self.document_action_group.change_action_state(name, state)
    }

    pub(crate) fn set_default_actions(&self) {
        let dual_mode = self.model.page_layout() == PageLayout::Dual;
        let document = self.document().unwrap();
        let info = document.info();

        if info.is_none() {
            self.set_action_enabled("show-properties", false);
        }

        if !document.is::<papers_document::Selection>() {
            self.set_action_enabled("select-all", false);
        }

        if !document.is::<papers_document::DocumentFind>() {
            self.set_action_enabled("find", false);
            self.set_action_enabled("toggle-find", false);
        }

        let can_add_annotation = document
            .dynamic_cast_ref::<DocumentAnnotations>()
            .is_some_and(|d| d.can_add_annotation());

        if can_add_annotation {
            let item = gio::MenuItem::new(None, None);

            item.set_attribute_value("custom", Some(&"palette".into()));
            self.annot_menu.insert_item(0, &item);

            self.view_popup
                .add_child(&self.annot_menu_child.get(), "palette");
        } else {
            self.set_action_enabled("add-text-annotation", false);
            self.set_action_enabled("annot-textmarkup-type", false);
        }

        let can_sign = document
            .dynamic_cast_ref::<DocumentSignatures>()
            .map(|doc| doc.can_sign())
            .unwrap_or_default();

        self.set_action_enabled("digital-signing", can_sign);
        self.set_action_enabled("manual-signing", can_add_annotation);

        if can_add_annotation {
            self.setup_signature_context_menu();
        }

        self.set_action_enabled("dual-odd-left", dual_mode);

        self.set_action_enabled("zoom-in", self.view.can_zoom_in());
        self.set_action_enabled("zoom-out", self.view.can_zoom_out());

        // Set enabled state for go-back-history and go-forward-history
        self.set_action_enabled(
            "go-back-history",
            !self.history.is_frozen() && self.history.can_go_back(),
        );

        self.set_action_enabled(
            "go-forward-history",
            !self.history.is_frozen() && self.history.can_go_forward(),
        );

        // Set enabled state for caret-navigation
        self.set_action_enabled("caret-navigation", self.view.supports_caret_navigation());

        self.doc_restrictions_changed();
    }

    pub fn setup_actions(&self) {
        let actions = [
            gio::ActionEntryBuilder::new("open-copy")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.open_copy_at_dest(None);
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
            gio::ActionEntryBuilder::new("show-sidebar")
                .state(glib::Variant::from(true))
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let show_side_pane = state.unwrap().get::<bool>().unwrap();
                        action.set_state(&glib::Variant::from(show_side_pane));
                        obj.split_view.set_show_sidebar(show_side_pane);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("select-page")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.page_selector.grab_focus();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("print")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.action_menu_button.popdown();
                        obj.print_range(1, obj.document().unwrap().n_pages());
                    }
                ))
                .build(),
            // Document related actions
            gio::ActionEntryBuilder::new("save")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_save();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("save-as")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_save_as();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("show-properties")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_file_properties();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("continuous")
                .state(true.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let state = state.unwrap();
                        let continuous = state.get::<bool>().unwrap();

                        obj.model.set_continuous(continuous);
                        action.set_state(state);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("rtl")
                .state(false.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let state = state.unwrap();
                        let rtl = state.get::<bool>().unwrap();

                        obj.model.set_rtl(rtl);
                        action.set_state(state);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("dual-odd-left")
                .state(false.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let state = state.unwrap();
                        let dual_odd_left = state.get::<bool>().unwrap();

                        obj.model.set_dual_page_odd_pages_left(dual_odd_left);
                        action.set_state(state);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("dual-page")
                .state(false.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let state = state.unwrap();
                        let dual_page = state.get::<bool>().unwrap();

                        obj.model.set_page_layout(if dual_page {
                            PageLayout::Dual
                        } else {
                            PageLayout::Single
                        });

                        let has_pages = obj.document().map(|d| d.n_pages() > 0).unwrap_or_default();

                        obj.set_action_enabled("dual-odd-left", dual_page && has_pages);

                        action.set_state(state);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("select-all")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.select_all();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("zoom-in")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.model.set_sizing_mode(papers_view::SizingMode::Free);
                        obj.view.zoom_in();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("zoom-out")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.model.set_sizing_mode(papers_view::SizingMode::Free);
                        obj.view.zoom_out();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("zoom")
                .parameter_type(Some(glib::VariantTy::DOUBLE))
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, scale| {
                        let scale = scale.and_then(|d| d.get::<f64>()).unwrap();
                        obj.model.set_sizing_mode(papers_view::SizingMode::Free);
                        obj.model.set_scale(scale);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("sizing-mode")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state(glib::Variant::from("free"))
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let mode = state.and_then(|s| s.str()).unwrap();

                        let mode = match mode {
                            "fit-width" => SizingMode::FitWidth,
                            "fit-page" => SizingMode::FitPage,
                            "free" => SizingMode::Free,
                            "automatic" => SizingMode::Automatic,
                            _ => panic!(),
                        };

                        obj.model.set_sizing_mode(mode);
                        action.set_state(state.unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("annot-color")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state(glib::Variant::from(&AnnotationColor::Yellow.to_string()))
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_annot_color(state.unwrap().str().unwrap());
                        action.set_state(state.unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("annot-textmarkup-type")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state(glib::Variant::from("highlight"))
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_annot_textmarkup_type(state.unwrap().str().unwrap());
                        action.set_state(state.unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("enable-spellchecking")
                .state(false.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let enabled = state.and_then(|v| v.get::<bool>()).unwrap();

                        obj.view.set_enable_spellchecking(enabled);
                        action.set_state(state.unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-previous-page")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.previous_page();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-next-page")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.next_page();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-first-page")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let old_page = obj.model.page();
                        obj.model.set_page(0);
                        if old_page >= 0 {
                            obj.history.add_page(old_page);
                            obj.history.add_page(0);
                        }
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-last-page")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let old_page = obj.model.page();
                        let new_page = obj.document().unwrap().n_pages();

                        obj.model.set_page(new_page);

                        if old_page >= 0 && new_page >= 0 {
                            obj.history.add_page(old_page);
                            obj.history.add_page(new_page);
                        }
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("rotate-left")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| obj.cmd_rotate_left()
                ))
                .build(),
            gio::ActionEntryBuilder::new("rotate-right")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| obj.cmd_rotate_right()
                ))
                .build(),
            gio::ActionEntryBuilder::new("copy")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| obj.view.copy()
                ))
                .build(),
            gio::ActionEntryBuilder::new("undo")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.undo_context.undo();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("redo")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.undo_context.redo();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("caret-navigation")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_view_toggle_caret_navigation();
                    }
                ))
                .build(),
            // popup menu actions
            gio::ActionEntryBuilder::new("open-link")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.handle_link(obj.link.borrow().as_ref().unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("send-email")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.handle_link(obj.link.borrow().as_ref().unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-to-link")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view.handle_link(obj.link.borrow().as_ref().unwrap());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("open-link-new-window")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(dest) = obj
                            .link
                            .borrow()
                            .as_ref()
                            .and_then(|l| l.action())
                            .and_then(|action| action.dest())
                        {
                            obj.open_copy_at_dest(Some(&dest));
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("copy-link-address")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(action) = obj.link.borrow().as_ref().and_then(|l| l.action()) {
                            obj.view.copy_link_address(&action);
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("copy-email-address")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(action) = obj.link.borrow().as_ref().and_then(|l| l.action())
                            && let Some(uri) = action.uri()
                            && let Some(email) = uri.strip_prefix("mailto:")
                        {
                            obj.obj().clipboard().set_text(email);
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("copy-image")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_copy_image();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("save-image")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_save_image_as();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("remove-annot")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(annot) = obj.annot.borrow().as_ref() {
                            obj.annots_context.remove_annotation(annot);

                            // Show toast notification with undo button
                            let toast = adw::Toast::builder()
                                .title(gettext("Annotation deleted"))
                                .button_label(gettext("_Undo"))
                                .timeout(7)
                                .build();

                            toast.connect_button_clicked(glib::clone!(
                                #[weak]
                                obj,
                                move |_| {
                                    obj.undo_context.undo();
                                }
                            ));

                            // Clear the stored reference when toast is dismissed
                            toast.connect_dismissed(glib::clone!(
                                #[weak]
                                obj,
                                move |_| {
                                    obj.deletion_toast.replace(None);
                                }
                            ));

                            // Store reference to dismiss later if needed
                            obj.deletion_toast.replace(Some(toast.clone()));

                            obj.toast_overlay.add_toast(toast);
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("edit-annot")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(annot) = obj.annot.borrow().as_ref() {
                            obj.view.open_annotation_editor(annot);
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("digital-signing")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.create_certificate_selection();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("manual-signing")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.open_sign_manually_dialog();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("create-signature")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.view_popup.popdown();
                        let dialog = obj.open_sign_manually_dialog();
                        dialog.navigate_to_new_signature();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("add-manual-signature")
                .parameter_type(Some(glib::VariantTy::STRING))
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, param| {
                        if let Some(signature_id) = param.and_then(|p| p.str()) {
                            obj.cmd_add_manual_signature(signature_id);
                        }
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("add-text-annotation")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_add_text_annotation();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("toggle-edit-mode")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_toggle_edit_mode();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("eraser-radius")
                .parameter_type(Some(glib::VariantTy::DOUBLE))
                .state((5.).into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_radius_state(action, state.unwrap());
                        obj.eraser_radius_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("pen-stroke")
                .parameter_type(Some(glib::VariantTy::DOUBLE))
                .state((1.).into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_radius_state(action, state.unwrap());
                        obj.pencil_stroke_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("highlight-stroke")
                .parameter_type(Some(glib::VariantTy::DOUBLE))
                .state((5.).into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_radius_state(action, state.unwrap());
                        obj.highlight_stroke_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("eraser-mode-objects")
                // blueprint does not support non-string target, see https://gitlab.gnome.org/GNOME/blueprint-compiler/-/issues/137, so using string instead of bool for now
                .parameter_type(Some(glib::VariantTy::STRING))
                .state("true".into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        if let Some(annotation_model) = obj.model.annotation_model() {
                            let obj = state.unwrap().get::<String>().unwrap() == "true";
                            annotation_model.set_property("eraser-objects", obj);
                            action.set_state(state.unwrap());
                        }
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("pen-color")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state("blue".into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_pen_color_state(action, AnnotationTool::Pencil, state.unwrap());
                        obj.pencil_color_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("highlight-color")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state("yellow".into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_pen_color_state(action, AnnotationTool::Highlight, state.unwrap());
                        obj.highlight_color_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("text-color")
                .parameter_type(Some(glib::VariantTy::STRING))
                .state("blue".into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        obj.cmd_pen_color_state(action, AnnotationTool::Text, state.unwrap());
                        obj.text_color_popover.popdown();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("open-attachment")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_open_attachment();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("save-attachment")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_save_attachment_as();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("annot-properties")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.cmd_annot_properties();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("open-with")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        if let Some(file) = obj.file.borrow().clone() {
                            let launcher = gtk::FileLauncher::new(Some(&file));

                            launcher.set_always_ask(true);

                            let parent = obj.parent_window();
                            launcher.launch(
                                parent.as_ref(),
                                gio::Cancellable::NONE,
                                |_| {},
                            );
                        };
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("toggle-find")
                .state(false.into())
                .change_state(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, action, state| {
                        let state = state.unwrap();
                        let show = state.get::<bool>().unwrap();
                        let is_shown = action.state().unwrap().get::<bool>().unwrap();
                        let search_context = obj.search_context.borrow();

                        if show {
                            obj.show_find_bar();
                            if !is_shown {
                                search_context.as_ref().unwrap().activate();
                            }
                        } else {
                            obj.close_find_bar();
                            if is_shown {
                                search_context.as_ref().unwrap().release();
                            }
                        }

                        action.set_state(state);
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("find")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let selected_text = obj.view.selected_text().filter(|t| !t.is_empty());
                        if let Some(selected_text) = selected_text {
                            obj.search_context
                                .borrow()
                                .as_ref()
                                .unwrap()
                                .set_search_term(&selected_text);
                            obj.find_restart();
                        }
                        obj.document_action_group
                            .change_action_state("toggle-find", &true.into());
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("find-next")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.find_sidebar.next();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("find-previous")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.find_sidebar.previous();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-back-history")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let page = obj.model.page();

                        if page >= 0 {
                            obj.history.add_page(page);
                        }

                        obj.history.go_back();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-forward-history")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        obj.history.go_forward();
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-forward")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let n_pages = obj.document().unwrap().n_pages();
                        let current_page = obj.model.page();

                        if current_page + 10 < n_pages {
                            obj.model.set_page(current_page + 10);
                        } else {
                            obj.model.set_page(n_pages - 1);
                        }
                    }
                ))
                .build(),
            gio::ActionEntryBuilder::new("go-backwards")
                .activate(glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    move |_, _, _| {
                        let current_page = obj.model.page();

                        if current_page - 10 >= 0 {
                            obj.model.set_page(current_page - 10);
                        } else {
                            obj.model.set_page(0);
                        }
                    }
                ))
                .build(),
        ];

        let group = self.document_action_group.clone();
        group.add_action_entries(actions);
        self.obj().insert_action_group("doc", Some(&group));
    }

    fn cmd_file_properties(&self) {
        let properties = PpsPropertiesWindow::new();

        properties.set_document(self.model.document());
        AdwDialogExt::present(&properties, Some(self.obj().as_ref()));
    }

    fn cmd_escape(&self) {
        if self
            .sidebar_stack
            .visible_child()
            .is_some_and(|w| w == *self.find_sidebar)
            && self.find_sidebar.focus_child().is_some()
        {
            WidgetExt::activate_action(self.obj().as_ref(), "doc.toggle-find", None).unwrap();
        } else if self.parent_window().is_some_and(|w| w.is_fullscreen()) {
            if let Some(win) = self.parent_window() {
                if let Ok(ag) = win.dynamic_cast::<gio::ActionGroup>() {
                    ag.change_action_state("fullscreen", &false.into());
                }
            }
        } else if self.split_view.is_collapsed() && self.split_view.shows_sidebar() {
            self.split_view.set_show_sidebar(false);
        } else if self.model.annotation_editing_state() != papers_view::AnnotationEditingState::NONE
        {
            self.cmd_toggle_edit_mode();
        }
    }

    fn cmd_annot_color(&self, color: &str) {
        let rgba = AnnotationColor::from(color).to_rgba();

        self.set_annot_textmarkup_icon_color(&rgba);

        let annot = self.annot.borrow().clone();
        if let Some(annot) = annot
            && annot.rgba() != rgba
        {
            annot.set_rgba(&rgba);
        }
    }

    fn cmd_annot_textmarkup_type(&self, markup_type: &str) {
        let markup_type = match markup_type {
            "highlight" => Some(AnnotationTextMarkupType::Highlight),
            "squiggly" => Some(AnnotationTextMarkupType::Squiggly),
            "strikethrough" => Some(AnnotationTextMarkupType::StrikeOut),
            "underline" => Some(AnnotationTextMarkupType::Underline),
            "none" => None,
            _ => panic!("unknown markup_type {markup_type}"),
        };

        if markup_type.is_none() {
            return;
        }

        let annot = self.annot.borrow().clone();
        if self.view.has_selection() {
            let selections = self.view.selections();
            for sel in selections.iter() {
                let mut start_point = papers_document::Point::new();
                let mut end_point = papers_document::Point::new();
                let markup_type = markup_type.expect("no markup_type, but an annot is set");
                start_point.set_x(sel.rect().x1());
                start_point.set_y(sel.rect().y1());
                end_point.set_x(sel.rect().x2());
                end_point.set_y(sel.rect().y2());
                let annot = self.annots_context.add_annotation_sync(
                    sel.page(),
                    papers_document::AnnotationType::TextMarkup,
                    &start_point,
                    &end_point,
                    &self.rgba_from_annot_color(),
                    AddAnnotationData::TextMarkup(markup_type),
                );
                self.annot.replace(annot.as_ref().cloned());
            }
        } else if let Some(annot) = annot
            && let Some(annot) = annot.dynamic_cast_ref::<AnnotationTextMarkup>()
        {
            let markup_type = markup_type.expect("no markup_type, but an annot is set");
            if annot.markup_type() != markup_type {
                annot.set_markup_type(markup_type);
            }
        }
    }

    fn set_annot_textmarkup_icon_color(&self, color: &gdk::RGBA) {
        let css_color = color.to_string();
        let css = format!("#annot-type-container {{--annot-textmarkup-color: {css_color}; }}");

        let provider = gtk::CssProvider::new();
        provider.load_from_string(&css);

        match gdk::Display::default() {
            Some(display) => {
                gtk::style_context_add_provider_for_display(
                    &display,
                    &provider,
                    gtk::STYLE_PROVIDER_PRIORITY_APPLICATION,
                );
            }
            _ => glib::g_critical!("", "Could not find a display"),
        }
    }

    fn rotate(&self, degree: i32) {
        let rotation = self.model.rotation() + degree;
        self.model.set_rotation(rotation);
    }

    fn cmd_rotate_left(&self) {
        self.rotate(-90);
    }

    fn cmd_rotate_right(&self) {
        self.rotate(90);
    }

    fn cmd_save(&self) {
        self.save();
    }

    fn cmd_save_as(&self) {
        self.save_as();
    }

    fn cmd_save_image_as(&self) {
        let Some(image) = self.image.borrow().clone() else {
            return;
        };

        // We simply give user a default name here. The extension is not hardcoded
        // and we will detect the target file extension to determine the format.
        // We will fallback to png or jpeg when no extension is specified.
        let initial_name = glib::DateTime::now_local()
            .unwrap()
            .format("%c.png")
            .unwrap();

        let dialog = gtk::FileDialog::builder()
            .title(gettext("Save Image"))
            .modal(true)
            .initial_name(initial_name)
            .build();

        self.file_dialog_restore_folder(&dialog, UserDirectory::Pictures);

        glib::spawn_future_local(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            async move {
                let parent = obj.parent_window();
                let Ok(file) = dialog.save_future(parent.as_ref()).await else {
                    return;
                };

                obj.file_dialog_save_folder(Some(&file), UserDirectory::Pictures);

                let mut format = gdk_pixbuf_format_by_extension(&file.uri());

                if format.is_none()
                    && file
                        .path()
                        .map(|p| p.extension().is_some())
                        .unwrap_or_default()
                {
                    // no extension found and no extension provided within uri
                    format = gdk_pixbuf_format_by_extension(".png").or(
                        // no .png support, try .jpeg
                        gdk_pixbuf_format_by_extension(".jpeg"),
                    );
                }

                let Some(format) = format else {
                    obj.error_message(
                        None,
                        &gettext("Couldn’t find appropriate format to save image"),
                    );
                    return;
                };

                let pixbuf = obj
                    .document()
                    .and_dynamic_cast::<DocumentImages>()
                    .ok()
                    .and_then(|d| d.image(&image))
                    .unwrap();

                if let Err(e) = pixbuf.save_to_streamv(
                    &file
                        .replace(
                            None,
                            false,
                            gio::FileCreateFlags::REPLACE_DESTINATION,
                            gio::Cancellable::NONE,
                        )
                        .unwrap(),
                    &format.name().unwrap(),
                    &[],
                    gio::Cancellable::NONE,
                ) {
                    obj.error_message(Some(&e), &gettext("The image could not be saved."));
                    return;
                }
            }
        ));
    }

    fn cmd_copy_image(&self) {
        let Some(image) = self.image.borrow().clone() else {
            return;
        };
        let clipboard = self.obj().clipboard();

        let pixbuf = self
            .document()
            .and_dynamic_cast::<papers_document::DocumentImages>()
            .unwrap()
            .image(&image);

        clipboard.set_texture(&gdk::Texture::for_pixbuf(&pixbuf.unwrap()))
    }

    fn annot_properties_dialog_response(&self, dialog: &PpsAnnotationPropertiesDialog) {
        let annot = dialog.annotation();
        let author = dialog.author();
        let rgba = dialog.rgba();
        let opacity = dialog.opacity();
        let popup_is_open = dialog.popup_open();
        let _notify = annot.freeze_notify();

        // Set annotations changes
        if let Some(annot) = annot.dynamic_cast_ref::<AnnotationMarkup>() {
            annot.set_label(&author);
            annot.set_rgba(&rgba);
            annot.set_opacity(opacity);
            annot.set_popup_is_open(popup_is_open);
        }

        if let Some(annot) = annot.dynamic_cast_ref::<AnnotationText>() {
            annot.set_icon(dialog.text_icon());
        }

        if let Some(annot) = annot.dynamic_cast_ref::<AnnotationTextMarkup>() {
            annot.set_markup_type(dialog.markup_type());
        }
    }

    fn cmd_annot_properties(&self) {
        let Some(annot) = self.annot.borrow().clone() else {
            return;
        };

        let dialog = PpsAnnotationPropertiesDialog::new(&annot);

        dialog.connect_closure(
            "changed",
            true,
            glib::closure_local!(
                #[weak(rename_to = obj)]
                self,
                move |dialog| { obj.annot_properties_dialog_response(dialog) }
            ),
        );
        dialog.present(Some(self.obj().as_ref()));
    }

    fn cmd_view_toggle_caret_navigation(&self) {
        let enabled = self.view.is_caret_navigation_enabled();

        self.set_caret_navigation_enabled(!enabled);

        let msg = if !enabled {
            gettext("Caret navigation mode is now enabled, press F7 to disable.")
        } else {
            gettext("Caret navigation mode is now disabled, press F7 to enable.")
        };

        let toast = adw::Toast::builder().title(&msg).timeout(5).build();

        self.toast_overlay.dismiss_all();
        self.toast_overlay.add_toast(toast);
    }

    fn rgba_from_annot_color(&self) -> gdk::RGBA {
        let binding = self
            .document_action_group
            .lookup_action("annot-color")
            .unwrap()
            .state()
            .unwrap();
        let color = binding.str().unwrap();
        AnnotationColor::from(color).to_rgba()
    }

    fn cmd_add_text_annotation(&self) {
        let (x, y);
        if let Some((px, py)) = Document::misc_get_pointer_position(&self.view.get()) {
            x = px;
            y = py
        } else {
            // Check if the pointer is not over the current surface, then
            // it should be in the popover, and we should get the point
            // from where the popover is pointing

            let (_, rect) = self.view_popup.pointing_to();
            x = rect.x();
            y = rect.y();
        };

        if let Some(doc_point) = self.view.document_point_for_view_point(x.into(), y.into()) {
            _ = self.annots_context.add_annotation_sync(
                doc_point.page_index(),
                papers_document::AnnotationType::Text,
                &doc_point.point_on_page(),
                &doc_point.point_on_page(),
                &self.rgba_from_annot_color(),
                AddAnnotationData::None,
            );
        };
    }

    pub(super) fn cmd_add_manual_signature(&self, signature_id: &str) {
        let (x, y);
        if let Some((px, py)) = Document::misc_get_pointer_position(&self.view.get()) {
            x = px;
            y = py;
        } else {
            let (_, rect) = self.view_popup.pointing_to();
            x = rect.x();
            y = rect.y();
        };

        if let Some(doc_point) = self.view.document_point_for_view_point(x.into(), y.into()) {
            self.apply_signature_to_document(
                signature_id,
                doc_point.page_index(),
                Some(&doc_point.point_on_page()),
            );
        }

        self.view_popup.popdown();
    }

    fn setup_signature_context_menu(&self) {
        if self.signature_menu.n_items() > 0 {
            return;
        }

        self.manage_signatures_button.connect_clicked(glib::clone!(
            #[weak(rename_to=obj)]
            self,
            move |_| obj.view_popup.popdown()
        ));
        let submenu = gio::Menu::new();

        let previews_section = gio::Menu::new();
        let custom_item = gio::MenuItem::new(None, None);
        custom_item.set_attribute_value("custom", Some(&"signature-previews".into()));
        previews_section.insert_item(0, &custom_item);
        submenu.append_section(None, &previews_section);

        let submenu_item =
            gio::MenuItem::new_submenu(Some(&gettext("Add _Visual Signature")), &submenu);
        self.signature_menu.insert_item(0, &submenu_item);

        self.view_popup
            .add_child(&self.signature_menu_child.get(), "signature-previews");

        let signature_manager = self
            .signature_manager
            .get_or_init(crate::signature_manager::PpsSignatureManager::new);

        self.refresh_signature_previews();

        signature_manager.connect_closure(
            "signatures-list-changed",
            false,
            glib::closure_local!(
                #[weak(rename_to = obj)]
                self,
                move |_: &crate::signature_manager::PpsSignatureManager| {
                    obj.refresh_signature_previews();
                }
            ),
        );
    }

    fn refresh_signature_previews(&self) {
        glib::spawn_future_local(glib::clone!(
            #[weak(rename_to=obj)]
            self,
            async move {
                let manager = obj.signature_manager.get().unwrap();
                let container = obj.signature_menu_child.get();
                while let Some(child) = container.first_child() {
                    container.remove(&child);
                }

                let signatures = manager.list_signatures().await;

                if signatures.is_empty() {
                    container.append(&obj.signature_menu_no_signature.get());
                    return;
                }

                for sig in &signatures {
                    if let Some(pixbuf) = manager.get_signature_pixbuf(&sig.id) {
                        let max_width = 120.0_f64;
                        let max_height = 48.0_f64;
                        let scale = (max_width / pixbuf.width() as f64)
                            .min(max_height / pixbuf.height() as f64)
                            .min(1.0);
                        let new_width = (pixbuf.width() as f64 * scale) as i32;
                        let new_height = (pixbuf.height() as f64 * scale) as i32;

                        if let Some(scaled) = pixbuf.scale_simple(
                            new_width,
                            new_height,
                            gdk_pixbuf::InterpType::Bilinear,
                        ) {
                            let texture = gdk::Texture::for_pixbuf(&scaled);
                            let picture = gtk::Picture::builder()
                                .paintable(&texture)
                                .can_shrink(false)
                                .content_fit(gtk::ContentFit::Contain)
                                .build();
                            picture.add_css_class("signature-preview");

                            let button = gtk::Button::builder()
                                .child(&picture)
                                .action_name("doc.add-manual-signature")
                                .action_target(&sig.id.to_variant())
                                .css_classes(vec!["flat"])
                                .build();

                            container.append(&button);
                        }
                    }
                }
                container.append(&obj.signature_menu_manage_child.get());
            }
        ));
    }

    pub(super) fn update_edit_toolbar_visibility(&self, visible: bool) {
        if visible {
            self.draw_revealer.set_reveal_child(true);
            self.draw_revealer
                .parent()
                .unwrap()
                .add_css_class("expanded");
            self.draw_button.remove_css_class("circular");
            self.draw_button.remove_css_class("flat");
            self.draw_button.set_icon_name("window-close-symbolic");
            self.draw_button
                .set_tooltip_text(Some(&gettext("Close Menu")));
        } else {
            self.draw_revealer.set_reveal_child(false);
            self.draw_button.set_icon_name("document-edit-symbolic");
            self.draw_button.add_css_class("circular");
            self.draw_button.add_css_class("flat");
            self.draw_revealer
                .parent()
                .unwrap()
                .remove_css_class("expanded");
            self.draw_button
                .set_tooltip_text(Some(&gettext("Edit Document")));
        }
    }

    pub fn cmd_toggle_edit_mode(&self) {
        let editing_state = self.model.annotation_editing_state();
        // Just toggle the state - the notify signal will handle UI updates
        if editing_state == papers_view::AnnotationEditingState::NONE
            || editing_state == papers_view::AnnotationEditingState::STAMP
        {
            let is_text = self
                .model
                .annotation_model()
                .map(|m| m.tool() == AnnotationTool::Text)
                .unwrap_or(false);

            if is_text {
                self.model.set_annotation_editing_state(
                    papers_view::AnnotationEditingState::INSERT_TEXT
                        .union(papers_view::AnnotationEditingState::TEXT),
                );
            } else {
                self.model
                    .set_annotation_editing_state(papers_view::AnnotationEditingState::INK);
            }
        } else {
            self.model
                .set_annotation_editing_state(papers_view::AnnotationEditingState::NONE);
        }
    }

    pub fn cmd_radius_state(&self, action: &gio::SimpleAction, state: &glib::Variant) {
        let Some(annotation_model) = self.model.annotation_model() else {
            return;
        };
        let radius = state.get::<f64>().unwrap();

        match annotation_model.tool() {
            AnnotationTool::Pencil => annotation_model.set_pen_radius(radius),
            AnnotationTool::Highlight => annotation_model.set_highlight_radius(radius),
            AnnotationTool::Eraser => annotation_model.set_eraser_radius(radius),
            _ => (),
        }

        action.set_state(state);
    }

    pub fn cmd_pen_color_state(
        &self,
        action: &gio::SimpleAction,
        tool: AnnotationTool,
        state: &glib::Variant,
    ) {
        let Some(annotation_model) = self.model.annotation_model() else {
            return;
        };
        let color = state.get::<String>().unwrap();
        let mut rgba = match color.as_str() {
            "yellow" => gdk::RGBA::parse("#f5c211").unwrap(),
            "black" => gdk::RGBA::parse("#000000").unwrap(),
            "orange" => gdk::RGBA::parse("#ff7800").unwrap(),
            "red" => gdk::RGBA::parse("#ed333b").unwrap(),
            "purple" => gdk::RGBA::parse("#c061cb").unwrap(),
            "blue" => gdk::RGBA::parse("#3584e4").unwrap(),
            "green" => gdk::RGBA::parse("#33d17a").unwrap(),
            _ => panic!("Unexpected color"),
        };

        match tool {
            AnnotationTool::Pencil => annotation_model.set_pen_color(&rgba),
            AnnotationTool::Highlight => {
                rgba.set_alpha(0.7);
                annotation_model.set_highlight_color(&rgba);
            }
            AnnotationTool::Eraser => {
                // FIXME: should be hidden
            }
            AnnotationTool::Text => {
                annotation_model.set_text_color(&rgba);
            }
            _ => panic!("Unexpected tool"),
        }

        action.set_state(state);
    }

    fn cmd_open_attachment(&self) {
        let Some(attachment) = self.attachment.take() else {
            return;
        };
        let context = self.obj().display().app_launch_context();

        if let Err(e) = attachment.open(&context)
            && !e.matches(gtk::DialogError::Dismissed)
        {
            self.error_message(Some(&e), &gettext("Unable to Open Attachment"));
        }
    }

    fn cmd_save_attachment_as(&self) {
        let Some(attachment) = self.attachment.borrow().clone() else {
            return;
        };
        let attachments = gio::ListStore::new::<Attachment>();

        attachments.append(&attachment);

        self.reset_progress_cancellable();

        let dialog = gtk::FileDialog::builder()
            .title(pgettext("This is an action", "Save Attachment"))
            .modal(true)
            .build();

        self.file_dialog_restore_folder(&dialog, UserDirectory::Documents);

        let window = self.parent_window();

        glib::spawn_future_local(glib::clone!(
            #[weak(rename_to = obj)]
            self,
            async move {
                if let Err(e) = obj
                    .attachment_context
                    .save_attachments_future(attachments, window.as_ref())
                    .await
                {
                    obj.error_message(Some(&e), &gettext("The attachment could not be saved."));
                }
            }
        ));
    }
}
