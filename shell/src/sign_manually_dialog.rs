use crate::deps::*;
use crate::signature_manager::PpsSignatureManager;
use std::cell::RefCell;

mod imp {
    use std::cell::OnceCell;

    use super::*;

    #[derive(Debug, Default, CompositeTemplate)]
    #[template(resource = "/org/gnome/papers/ui/sign-manually-dialog.ui")]
    pub struct PpsSignManuallyDialog {
        // PAGE 1: Signature chooser children
        #[template_child]
        pub(super) navigation_view: TemplateChild<adw::NavigationView>,
        #[template_child]
        pub(super) toast_overlay: TemplateChild<adw::ToastOverlay>,
        #[template_child]
        pub(super) signature_chooser:
            TemplateChild<crate::visual_signature_chooser::PpsVisualSignatureChooser>,

        // PAGE 2: Drawing widget
        #[template_child]
        pub(super) signature_drawing_widget:
            TemplateChild<crate::signature_drawing_widget::PpsSignatureDrawingWidget>,

        // State
        pub(super) editing_signature_id: RefCell<Option<String>>,
        pub(super) auto_save_timeout_id: RefCell<Option<glib::SourceId>>,

        height_animation: OnceCell<adw::SpringAnimation>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for PpsSignManuallyDialog {
        const NAME: &'static str = "PpsSignManuallyDialog";
        type Type = super::PpsSignManuallyDialog;
        type ParentType = adw::Dialog;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
        }

        fn instance_init(obj: &InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for PpsSignManuallyDialog {
        fn signals() -> &'static [glib::subclass::Signal] {
            static SIGNALS: std::sync::OnceLock<Vec<glib::subclass::Signal>> =
                std::sync::OnceLock::new();
            SIGNALS.get_or_init(|| {
                vec![
                    glib::subclass::Signal::builder("insert-signature")
                        .param_types([str::static_type()])
                        .build(),
                ]
            })
        }

        fn constructed(&self) {
            self.parent_constructed();
            // Insert button: save current drawing then emit signal and close
            self.signature_drawing_widget
                .insert_button()
                .connect_clicked(glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    move |_| {
                        imp.cancel_auto_save_timeout();
                        imp.auto_save_signature();
                        if let Some(sig_id) = imp.editing_signature_id.borrow().clone() {
                            imp.obj().emit_by_name::<()>("insert-signature", &[&sig_id]);
                            imp.obj().close();
                        }
                    }
                ));

            self.navigation_view
                .connect_visible_page_notify(glib::clone!(
                    #[weak(rename_to=imp)]
                    self,
                    move |_| imp.resize()
                ));

            // Reset form / update insert button when navigating to edit page
            self.navigation_view.connect_pushed(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |nav_view| {
                    if let Some(page) = nav_view.visible_page()
                        && page.tag().as_deref() == Some("new-signature")
                    {
                        imp.signature_drawing_widget
                            .insert_button()
                            .set_sensitive(imp.editing_signature_id.borrow().is_some());

                        if imp.editing_signature_id.borrow().is_none() {
                            imp.reset_new_signature_form();
                        }
                    }
                }
            ));

            self.navigation_view.connect_popped(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_nav_view, page| {
                    if page.tag().as_deref() == Some("new-signature") {
                        imp.signature_drawing_widget
                            .insert_button()
                            .set_sensitive(false);
                        imp.cancel_auto_save_timeout();
                        imp.auto_save_signature();

                        if let Some(signature_id) = imp.editing_signature_id.borrow().clone() {
                            imp.signature_chooser
                                .set_selected_signature(Some(signature_id));
                        }

                        imp.reset_new_signature_form();
                    }
                }
            ));

            // Connect to stroke-completed signal for auto-save
            self.signature_drawing_widget.connect_local(
                "stroke-completed",
                false,
                glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    #[upgrade_or]
                    None,
                    move |_values| {
                        imp.signature_drawing_widget
                            .insert_button()
                            .set_sensitive(true);
                        imp.schedule_auto_save();
                        None
                    }
                ),
            );

            // Connect to upload-completed signal for immediate auto-save
            self.signature_drawing_widget.connect_local(
                "upload-completed",
                false,
                glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    #[upgrade_or]
                    None,
                    move |_values| {
                        imp.signature_drawing_widget
                            .insert_button()
                            .set_sensitive(true);
                        imp.cancel_auto_save_timeout();
                        imp.auto_save_signature();
                        None
                    }
                ),
            );

            // Listen to chooser's edit-signature signal
            self.signature_chooser.connect_local(
                "edit-signature",
                false,
                glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    #[upgrade_or]
                    None,
                    move |values| {
                        let sig_id = values[1].get::<String>().unwrap();
                        obj.navigate_to_edit_signature(&sig_id);
                        None
                    }
                ),
            );

            // Listen to chooser's request-toast signal
            self.signature_chooser.connect_local(
                "request-toast",
                false,
                glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    #[upgrade_or]
                    None,
                    move |values| {
                        let toast = values[1].get::<adw::Toast>().unwrap();
                        imp.toast_overlay.add_toast(toast);
                        None
                    }
                ),
            );
        }
    }

    impl WidgetImpl for PpsSignManuallyDialog {}
    impl AdwDialogImpl for PpsSignManuallyDialog {}

    impl PpsSignManuallyDialog {
        fn schedule_auto_save(&self) {
            self.cancel_auto_save_timeout();

            let timeout_id = glib::timeout_add_local_once(
                std::time::Duration::from_secs(1),
                glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    move || {
                        imp.auto_save_timeout_id.take();
                        imp.auto_save_signature();
                    }
                ),
            );

            self.auto_save_timeout_id.replace(Some(timeout_id));
        }

        fn cancel_auto_save_timeout(&self) {
            if let Some(timeout_id) = self.auto_save_timeout_id.take() {
                timeout_id.remove();
            }
        }

        fn auto_save_signature(&self) {
            let widget = self.signature_drawing_widget.get();

            if !widget.has_signature() {
                log::debug!("Auto-save: No signature content, skipping");
                return;
            }

            let Some(manager) = self.signature_chooser.signature_manager() else {
                log::error!("Auto-save: No signature manager");
                return;
            };

            let name = match glib::DateTime::now_local() {
                Ok(dt) => match dt.format("%Y-%m-%d %H:%M:%S") {
                    Ok(formatted) => format!("Signature {}", formatted),
                    Err(_) => "My Signature".to_string(),
                },
                Err(_) => "My Signature".to_string(),
            };

            let pixbuf = match widget.get_pixbuf() {
                Ok(p) => p,
                Err(e) => {
                    log::error!("Auto-save: Failed to generate signature: {}", e);
                    return;
                }
            };

            let editing_id = self.editing_signature_id.borrow().clone();
            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to=obj)]
                self,
                async move {
                    let result: Result<(), String> = if let Some(ref editing_id) = editing_id {
                        manager
                            .update_signature(editing_id, &name, &pixbuf)
                            .await
                            .map(|_| {
                                log::info!(
                                    "Auto-save: Updated signature: {} ({})",
                                    name,
                                    editing_id
                                );
                            })
                            .map_err(|e| format!("Failed to update signature: {}", e))
                    } else {
                        manager
                            .add_signature(&name, &pixbuf)
                            .await
                            .map(|id| {
                                log::info!("Auto-save: Saved signature: {} ({})", name, id);
                                *obj.editing_signature_id.borrow_mut() = Some(id);
                            })
                            .map_err(|e| format!("Failed to save signature: {}", e))
                    };

                    if let Err(ref e) = result {
                        log::error!("Auto-save failed: {}", e);
                        let toast = adw::Toast::new(e);
                        obj.toast_overlay.add_toast(toast);
                    }
                }
            ));
        }

        fn reset_new_signature_form(&self) {
            self.signature_drawing_widget.reset();
            *self.editing_signature_id.borrow_mut() = None;
        }

        async fn load_signature_for_editing(&self, id: &str) -> Result<(), String> {
            let manager = self
                .signature_chooser
                .signature_manager()
                .ok_or_else(|| "No signature manager".to_string())?;

            let _sig = manager
                .get_signature(id)
                .await
                .ok_or_else(|| "Signature not found".to_string())?;

            if let Some(pixbuf) = manager.get_signature_pixbuf(id) {
                self.signature_drawing_widget.load_signature(pixbuf)?;
            } else {
                return Err("Signature file not found".to_string());
            }

            Ok(())
        }

        fn show_error(&self, message: &str) {
            let dialog = adw::AlertDialog::builder()
                .heading("Error")
                .body(message)
                .build();

            dialog.add_response("ok", "OK");
            dialog.set_default_response(Some("ok"));

            if let Some(parent) = self.obj().root().and_downcast::<gtk::Window>() {
                dialog.present(Some(&parent));
            }
        }

        pub fn navigate_to_new_signature(&self) {
            self.navigation_view.push_by_tag("new-signature");
        }

        fn navigate_to_edit_signature(&self, signature_id: &str) {
            *self.editing_signature_id.borrow_mut() = Some(signature_id.to_string());

            let sig_id = signature_id.to_string();
            glib::spawn_future_local(glib::clone!(
                #[weak(rename_to=obj)]
                self,
                async move {
                    if let Err(e) = obj.load_signature_for_editing(&sig_id).await {
                        log::error!("Failed to load signature for editing: {}", e);
                        obj.show_error(
                            &formatx!(&gettext("Failed to load signature: {}"), e)
                                .expect("Wrong format in translated string"),
                        );
                        *obj.editing_signature_id.borrow_mut() = None;
                        return;
                    }
                    obj.navigation_view.push_by_tag("new-signature");
                }
            ));
        }
        fn resize(&self) {
            // We don't animate the first resize as there is nothing to animate from.
            // Note that signatures are loaded asynchronously so right after the constructed
            // call, the widget may still be in an unfinished state.
            let (_, nat, _, _) = self
                .navigation_view
                .visible_page()
                .unwrap()
                .measure(gtk::Orientation::Vertical, self.navigation_view.width());
            if let Some(height_animation) = self.height_animation.get() {
                height_animation.pause();
                height_animation.set_value_from(self.obj().content_height() as f64);
                height_animation.set_value_to(nat as f64);
                height_animation.play();
            } else {
                self.obj().set_content_height(nat);
                self.height_animation
                    .set(adw::SpringAnimation::new(
                        &self.obj().clone(),
                        0.,
                        1.,
                        adw::SpringParams::new(1., 1., 1000.),
                        adw::PropertyAnimationTarget::new(&self.obj().clone(), "content-height"),
                    ))
                    .expect("cell already set");
            }
        }

        pub fn setup_signature_manager(&self, signature_manager: &PpsSignatureManager) {
            self.signature_chooser
                .set_signature_manager(Some(signature_manager.clone()));

            self.signature_chooser.connect_local(
                "signature-list-built",
                true,
                glib::clone!(
                    #[weak(rename_to = obj)]
                    self,
                    #[upgrade_or]
                    None,
                    move |_| {
                        obj.resize();
                        None
                    }
                ),
            );
        }
    }
}

glib::wrapper! {
    pub struct PpsSignManuallyDialog(ObjectSubclass<imp::PpsSignManuallyDialog>)
        @extends gtk::Widget, adw::Dialog,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

impl PpsSignManuallyDialog {
    pub fn new(signature_manager: &PpsSignatureManager) -> Self {
        let dialog: Self = glib::Object::builder().build();
        dialog.imp().setup_signature_manager(signature_manager);
        dialog
    }

    pub fn navigate_to_new_signature(&self) {
        self.imp().navigate_to_new_signature();
    }
}
