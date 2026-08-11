use crate::deps::*;
use crate::signature_manager::PpsSignatureManager;
use std::cell::Cell;

// Preview dimensions for signature list items
const SIGNATURE_PREVIEW_MAX_WIDTH: f64 = 120.0;
const SIGNATURE_PREVIEW_MAX_HEIGHT: f64 = 60.0;

mod imp {
    use super::*;

    #[derive(Debug, CompositeTemplate, glib::Properties)]
    #[template(resource = "/org/gnome/papers/ui/visual-signature-chooser.ui")]
    #[properties(wrapper_type = super::PpsVisualSignatureChooser)]
    pub struct PpsVisualSignatureChooser {
        #[template_child]
        pub(super) signatures_stack: TemplateChild<gtk::Stack>,
        #[template_child]
        pub(super) empty_state: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub(super) signatures_listbox: TemplateChild<gtk::ListBox>,
        #[template_child]
        pub(super) add_signature_row: TemplateChild<adw::ButtonRow>,

        // GObject properties
        // before we can pass properties; set_signature_manager acts as a one-time init.
        #[property(name = "signature-manager", get, set = Self::set_signature_manager, nullable)]
        pub(super) signature_manager: RefCell<Option<PpsSignatureManager>>,
        #[property(name = "selected-signature", get, set = Self::set_selected_signature_prop, nullable)]
        pub(super) selected_signature: RefCell<Option<String>>,
        // Radio buttons are hidden for manual signing (default) but shown in the digital
        #[property(name = "show-radio-buttons", get, set)]
        pub(super) show_radio_buttons: Cell<bool>,

        // Dummy radio button used as group anchor for radio button grouping
        pub(super) dummy_radio_button: gtk::CheckButton,
    }

    impl Default for PpsVisualSignatureChooser {
        fn default() -> Self {
            Self {
                signatures_stack: Default::default(),
                empty_state: Default::default(),
                signatures_listbox: Default::default(),
                add_signature_row: Default::default(),
                signature_manager: Default::default(),
                selected_signature: Default::default(),
                show_radio_buttons: Cell::new(false),
                dummy_radio_button: gtk::CheckButton::builder().visible(false).build(),
            }
        }
    }

    #[glib::object_subclass]
    impl ObjectSubclass for PpsVisualSignatureChooser {
        const NAME: &'static str = "PpsVisualSignatureChooser";
        type Type = super::PpsVisualSignatureChooser;
        type ParentType = gtk::Box;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.set_css_name("pps-visual-signature-chooser");
        }

        fn instance_init(obj: &InitializingObject<Self>) {
            obj.init_template();
        }
    }

    #[glib::derived_properties]
    impl ObjectImpl for PpsVisualSignatureChooser {
        fn signals() -> &'static [glib::subclass::Signal] {
            static SIGNALS: OnceLock<Vec<glib::subclass::Signal>> = OnceLock::new();
            SIGNALS.get_or_init(|| {
                vec![
                    glib::subclass::Signal::builder("selection-changed")
                        .param_types([Option::<String>::static_type()])
                        .build(),
                    glib::subclass::Signal::builder("signature-list-built").build(),
                    glib::subclass::Signal::builder("edit-signature")
                        .param_types([String::static_type()])
                        .build(),
                    glib::subclass::Signal::builder("request-toast")
                        .param_types([adw::Toast::static_type()])
                        .build(),
                ]
            })
        }

        fn constructed(&self) {
            self.parent_constructed();
        }
    }

    impl WidgetImpl for PpsVisualSignatureChooser {}
    impl BoxImpl for PpsVisualSignatureChooser {}

    impl PpsVisualSignatureChooser {
        fn set_signature_manager(&self, manager: Option<PpsSignatureManager>) {
            let Some(manager) = manager else {
                return;
            };

            manager.connect_closure(
                "signatures-list-changed",
                false,
                glib::closure_local!(
                    #[weak(rename_to = this)]
                    self,
                    move |_: &PpsSignatureManager| {
                        this.refresh_signature_list();
                    }
                ),
            );

            *self.signature_manager.borrow_mut() = Some(manager);

            self.refresh_signature_list();
        }

        fn set_selected_signature_prop(&self, id: Option<String>) {
            *self.selected_signature.borrow_mut() = id.clone();
            self.obj().notify("selected-signature");

            // Tick the matching radio button if an id was provided
            let Some(sig_id) = id else {
                return;
            };
            self.tick_radio_for_signature(&sig_id);
        }

        fn tick_radio_for_signature(&self, signature_id: &str) {
            let listbox = &self.signatures_listbox;
            let add_row = &*self.add_signature_row;
            let mut index = 0;
            while let Some(row) = listbox.row_at_index(index) {
                if row == *add_row {
                    break;
                }
                let row_sig_id: Option<String> = unsafe {
                    row.data::<String>("signature-id")
                        .as_ref()
                        .map(|ptr| ptr.as_ref().clone())
                };
                if row_sig_id.as_deref() == Some(signature_id) {
                    let radio: Option<gtk::CheckButton> = unsafe {
                        row.data::<gtk::CheckButton>("radio-button")
                            .as_ref()
                            .map(|ptr| ptr.as_ref().clone())
                    };
                    if let Some(radio) = radio {
                        radio.set_active(true);
                    }
                    return;
                }
                index += 1;
            }
        }

        // Refresh the signature list from backend (private - called via signal)
        pub(super) fn refresh_signature_list(&self) {
            let manager = {
                let borrow = self.signature_manager.borrow();
                let Some(m) = borrow.as_ref() else { return };
                m.clone()
            };

            // Clear existing signature rows synchronously before spawning
            let add_row = &*self.add_signature_row;
            loop {
                match self.signatures_listbox.row_at_index(0) {
                    None => break,
                    Some(row) if row == *add_row => break,
                    Some(row) => self.signatures_listbox.remove(&row),
                }
            }

            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to=imp)]
                self,
                async move {
                    let signatures = manager.list_signatures().await;
                    let obj = imp.obj();

                    if signatures.is_empty() {
                        imp.signatures_stack.set_visible_child(&*imp.empty_state);
                        *imp.selected_signature.borrow_mut() = None;
                        obj.notify("selected-signature");
                        obj.emit_by_name::<()>("selection-changed", &[&Option::<String>::None]);
                    } else {
                        imp.signatures_stack
                            .set_visible_child(&*imp.signatures_listbox);

                        for (index, sig) in signatures.iter().enumerate() {
                            let pixbuf = manager.get_signature_pixbuf(&sig.id);

                            let row = obj.create_signature_row(sig, pixbuf);
                            imp.signatures_listbox.insert(&row, index as i32);
                        }

                        // Restore selection if we have a previously selected signature
                        let maybe_selected = imp.selected_signature.borrow().clone();
                        if let Some(ref id) = maybe_selected {
                            imp.tick_radio_for_signature(id);
                        }
                    }
                    obj.emit_by_name::<()>("signature-list-built", &[]);
                }
            ));
        }
    }
}

glib::wrapper! {
    pub struct PpsVisualSignatureChooser(ObjectSubclass<imp::PpsVisualSignatureChooser>)
        @extends gtk::Box, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Orientable;
}

impl PpsVisualSignatureChooser {
    pub fn new(signature_manager: &PpsSignatureManager) -> Self {
        glib::Object::builder()
            .property("signature-manager", signature_manager)
            .build()
    }

    // Create a row widget for a signature
    fn create_signature_row(
        &self,
        sig: &crate::signature_manager::SignatureMetadata,
        pixbuf: Option<gdk::gdk_pixbuf::Pixbuf>,
    ) -> adw::ActionRow {
        let row = adw::ActionRow::builder().build();

        let imp = self.imp();
        let show_radio = imp.show_radio_buttons.get();
        let radio_button = gtk::CheckButton::builder()
            .group(&imp.dummy_radio_button)
            .valign(gtk::Align::Center)
            .visible(show_radio)
            .build();

        let signature_id = sig.id.clone();
        radio_button.connect_toggled(glib::clone!(
            #[weak(rename_to = chooser)]
            self,
            move |button| {
                if button.is_active() {
                    chooser.on_radio_button_selected(&signature_id);
                }
            }
        ));

        // Add signature image preview as prefix
        if let Some(pixbuf) = pixbuf {
            let scale = (SIGNATURE_PREVIEW_MAX_WIDTH / pixbuf.width() as f64)
                .min(SIGNATURE_PREVIEW_MAX_HEIGHT / pixbuf.height() as f64)
                .min(1.0);

            let new_width = (pixbuf.width() as f64 * scale) as i32;
            let new_height = (pixbuf.height() as f64 * scale) as i32;

            if let Some(scaled_pixbuf) =
                pixbuf.scale_simple(new_width, new_height, gdk::gdk_pixbuf::InterpType::Bilinear)
            {
                let texture = gdk::Texture::for_pixbuf(&scaled_pixbuf);
                let picture = gtk::Picture::builder()
                    .paintable(&texture)
                    .can_shrink(false)
                    .content_fit(gtk::ContentFit::Contain)
                    .width_request(new_width)
                    .height_request(new_height)
                    .build();

                picture.add_css_class("signature-preview");
                row.add_prefix(&picture);
            }
        }

        // Radio button goes leftmost; hidden for manual signing, shown for digital signatures.
        row.add_prefix(&radio_button);

        // Edit button
        let edit_button = gtk::Button::builder()
            .icon_name("document-edit-symbolic")
            .valign(gtk::Align::Center)
            .css_classes(vec!["flat", "circular"])
            .tooltip_text(gettext("Edit signature"))
            .build();

        let signature_id = sig.id.clone();
        edit_button.connect_clicked(glib::clone!(
            #[weak(rename_to = chooser)]
            self,
            move |_| {
                chooser.emit_edit_signature(&signature_id);
            }
        ));

        row.add_suffix(&edit_button);

        // Clicking anywhere on the row activates the edit button
        if show_radio {
            row.set_activatable_widget(Some(&radio_button));
        } else {
            row.set_activatable_widget(Some(&edit_button));
        }

        // Delete button
        let remove_button = gtk::Button::builder()
            .icon_name("user-trash-symbolic")
            .valign(gtk::Align::Center)
            .css_classes(vec!["flat", "circular"])
            .tooltip_text(gettext("Remove signature"))
            .build();

        let signature_id = sig.id.clone();
        remove_button.connect_clicked(glib::clone!(
            #[weak(rename_to = chooser)]
            self,
            move |_| {
                chooser.remove_signature_with_undo(&signature_id);
            }
        ));

        row.add_suffix(&remove_button);

        // Store signature ID and radio button reference
        unsafe {
            row.set_data("signature-id", sig.id.clone());
            row.set_data("radio-button", radio_button);
        }

        row
    }

    // Handle radio button selection
    fn on_radio_button_selected(&self, signature_id: &str) {
        *self.imp().selected_signature.borrow_mut() = Some(signature_id.to_string());
        self.notify("selected-signature");
        self.emit_by_name::<()>("selection-changed", &[&Some(signature_id.to_string())]);
    }

    // Emit signal requesting the parent to navigate to the edit page
    fn emit_edit_signature(&self, signature_id: &str) {
        self.emit_by_name::<()>("edit-signature", &[&signature_id.to_owned()]);
    }

    // Remove a signature with undo support
    fn remove_signature_with_undo(&self, signature_id: &str) {
        let sig_id = signature_id.to_string();
        glib::spawn_future_local(glib::clone!(
            #[weak(rename_to=obj)]
            self,
            async move {
                let manager = obj.signature_manager().unwrap();

                if let Err(e) = manager.mark_for_deletion(&sig_id).await {
                    log::error!("Failed to mark signature for deletion: {}", e);
                    obj.show_error_dialog("Failed to remove signature", &e.to_string());
                } else {
                    log::info!("Signature marked for deletion");
                    obj.show_undo_toast(&sig_id, &manager);
                }
            }
        ));
    }

    /// Build an undo toast and emit it via signal so the parent can display it
    fn show_undo_toast(&self, signature_id: &str, manager: &PpsSignatureManager) {
        let toast_duration = 5;
        let toast = adw::Toast::builder()
            .title(gettext("Signature Deleted"))
            .button_label(gettext("Undo"))
            .timeout(toast_duration)
            .build();

        let id_for_undo = signature_id.to_string();
        let id_for_delete = signature_id.to_string();

        let rm_timeout = glib::timeout_add_seconds_local_once(
            toast_duration,
            glib::clone!(
                #[weak]
                manager,
                move || {
                    log::info!("Toast dismissed - permanently deleting: {}", id_for_delete);
                    let id = id_for_delete.clone();
                    glib::spawn_future_local(glib::clone!(
                        #[weak]
                        manager,
                        async move {
                            if let Err(e) = manager.permanently_delete(&id).await {
                                log::error!("Failed to permanently delete signature: {}", e);
                            }
                        }
                    ));
                }
            ),
        )
        .as_raw();

        toast.connect_button_clicked(glib::clone!(
            #[weak]
            manager,
            move |_| {
                log::info!("Undo clicked - restoring signature: {}", id_for_undo);
                let id = id_for_undo.clone();
                unsafe {
                    let src_id: glib::SourceId = glib::translate::from_glib(rm_timeout);
                    src_id.remove();
                }
                glib::spawn_future_local(async move {
                    if let Err(e) = manager.restore_from_deletion(&id).await {
                        log::error!("Failed to restore signature: {}", e);
                    }
                });
            }
        ));

        self.emit_by_name::<()>("request-toast", &[&toast]);
    }

    fn show_error_dialog(&self, heading: &str, body: &str) {
        let dialog = adw::AlertDialog::builder()
            .heading(heading)
            .body(body)
            .build();

        dialog.add_response("ok", "OK");
        dialog.set_default_response(Some("ok"));

        if let Some(window) = self.root().and_downcast::<gtk::Window>() {
            dialog.present(Some(&window));
        }
    }

    pub fn connect_selection_changed<F>(&self, f: F) -> glib::SignalHandlerId
    where
        F: Fn(&Self, Option<String>) + 'static,
    {
        self.connect_local("selection-changed", false, move |values| {
            let chooser = values[0].get::<Self>().unwrap();
            let signature_id = values[1].get::<Option<String>>().unwrap();
            f(&chooser, signature_id);
            None
        })
    }
}
