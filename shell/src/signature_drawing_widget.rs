use crate::deps::*;
use crate::ink_transformation;
use crate::signature_image_processing;
use gdk::cairo;
use gdk::gdk_pixbuf;
use ink_stroke_modeler_rs::ModelerParams;
use std::cell::{Cell, RefCell};

#[derive(Clone, Debug)]
struct Stroke {
    points: Vec<(f64, f64)>,
    width: f64,
}

mod imp {
    use super::*;

    #[derive(Debug, Default, CompositeTemplate)]
    #[template(resource = "/org/gnome/papers/ui/signature-drawing-widget.ui")]
    pub struct PpsSignatureDrawingWidget {
        #[template_child]
        pub(super) drawing_area: TemplateChild<gtk::DrawingArea>,
        #[template_child]
        pub(super) placeholder_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub(super) undo_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) redo_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) insert_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) import_button: TemplateChild<gtk::Button>,

        // State: strokes drawn by the user
        pub(super) strokes: RefCell<Vec<Stroke>>,
        // Guards against concurrent imports racing on background_pixbuf
        pub(super) importing: Cell<bool>,
        // Background pixbuf in light-mode colors (black strokes on transparent)
        pub(super) background_pixbuf: RefCell<Option<gdk_pixbuf::Pixbuf>>,
        // Inverted copy for display in dark mode (white strokes on transparent)
        pub(super) dark_background_pixbuf: RefCell<Option<gdk_pixbuf::Pixbuf>>,

        pub(super) current_stroke: RefCell<Option<Stroke>>,
        pub(super) pen_width: Cell<f64>,
        pub(super) drag_start: Cell<Option<(f64, f64)>>,
        pub(super) undo_stack: RefCell<Vec<Vec<Stroke>>>,
        pub(super) redo_stack: RefCell<Vec<Vec<Stroke>>>,
        // Cache of completed strokes + background, rebuilt when strokes change
        pub(super) strokes_cache: RefCell<Option<cairo::ImageSurface>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for PpsSignatureDrawingWidget {
        const NAME: &'static str = "PpsSignatureDrawingWidget";
        type Type = super::PpsSignatureDrawingWidget;
        type ParentType = gtk::Box;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(obj: &InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for PpsSignatureDrawingWidget {
        fn signals() -> &'static [glib::subclass::Signal] {
            use std::sync::OnceLock;
            static SIGNALS: OnceLock<Vec<glib::subclass::Signal>> = OnceLock::new();
            SIGNALS.get_or_init(|| {
                vec![
                    glib::subclass::Signal::builder("stroke-completed").build(),
                    glib::subclass::Signal::builder("upload-completed").build(),
                ]
            })
        }

        fn constructed(&self) {
            self.parent_constructed();

            // Enable focus so keyboard shortcuts work
            self.obj().set_focusable(true);

            // Set default pen width to medium
            self.pen_width.set(4.0);

            self.setup_drawing_area();
            self.setup_undo_redo_buttons();
            self.setup_keyboard_shortcuts();
            self.setup_theme_change_handler();
            self.update_placeholder_visibility();
        }
    }

    impl WidgetImpl for PpsSignatureDrawingWidget {}
    impl BoxImpl for PpsSignatureDrawingWidget {}

    #[gtk::template_callbacks]
    impl PpsSignatureDrawingWidget {
        #[template_callback]
        pub(crate) fn on_choose_file_clicked(&self) {
            if self.importing.get() {
                log::debug!("Import already in progress, ignoring");
                return;
            }
            self.importing.set(true);

            log::debug!("Choose file clicked");

            let dialog = gtk::FileDialog::builder()
                .title(gettext("Choose Signature Image"))
                .modal(true)
                .build();

            let filter = gtk::FileFilter::new();
            filter.add_mime_type("image/png");
            filter.add_mime_type("image/jpeg");
            filter.add_mime_type("image/svg+xml");
            filter.set_name(Some(&gettext("Images")));

            let filters = gio::ListStore::new::<gtk::FileFilter>();
            filters.append(&filter);
            dialog.set_filters(Some(&filters));

            // Get the parent window
            let parent_window = self.obj().root().and_downcast::<gtk::Window>();

            dialog.open(
                parent_window.as_ref(),
                gio::Cancellable::NONE,
                glib::clone!(
                    #[weak(rename_to = imp)]
                    self,
                    move |result| {
                        imp.handle_file_chosen(result);
                    }
                ),
            );
        }

        // Set up the drawing area with event handlers
        fn setup_drawing_area(&self) {
            let drawing_area = self.drawing_area.get();

            drawing_area.set_draw_func(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_, cr, width, height| {
                    imp.draw_signature(cr, width, height);
                }
            ));

            // Set up gesture for drawing
            let gesture = gtk::GestureDrag::new();

            gesture.connect_drag_begin(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_, x, y| {
                    imp.on_drag_begin(x, y);
                }
            ));

            gesture.connect_drag_update(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_, x, y| {
                    imp.on_drag_update(x, y);
                }
            ));

            gesture.connect_drag_end(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_, _, _| {
                    imp.on_drag_end();
                }
            ));

            drawing_area.add_controller(gesture);
        }

        // Set up undo/redo buttons
        fn setup_undo_redo_buttons(&self) {
            self.undo_button.connect_clicked(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_| {
                    imp.undo();
                }
            ));

            self.redo_button.connect_clicked(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_| {
                    imp.redo();
                }
            ));
        }

        // Ctrl+Z for undo, Ctrl+Shift+Z for redo
        fn setup_keyboard_shortcuts(&self) {
            let controller = gtk::EventControllerKey::new();

            controller.connect_key_pressed(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                #[upgrade_or]
                glib::Propagation::Proceed,
                move |_, key, _, modifier| {
                    let ctrl = modifier.contains(gdk::ModifierType::CONTROL_MASK);
                    let shift = modifier.contains(gdk::ModifierType::SHIFT_MASK);

                    if ctrl && !shift && (key == gdk::Key::z || key == gdk::Key::Z) {
                        imp.undo();
                        glib::Propagation::Stop
                    } else if ctrl && shift && (key == gdk::Key::z || key == gdk::Key::Z) {
                        imp.redo();
                        glib::Propagation::Stop
                    } else {
                        glib::Propagation::Proceed
                    }
                }
            ));

            self.obj().add_controller(controller);
        }

        // Redraw the canvas when the theme changes so stroke colors adapt
        fn setup_theme_change_handler(&self) {
            let display = self.drawing_area.display();
            let style_manager = adw::StyleManager::for_display(&display);

            style_manager.connect_dark_notify(glib::clone!(
                #[weak(rename_to = imp)]
                self,
                move |_| {
                    imp.invalidate_strokes_cache();
                    imp.drawing_area.queue_draw();
                }
            ));
        }

        // Undo last stroke
        fn undo(&self) {
            let mut strokes = self.strokes.borrow_mut();
            if let Some(last_stroke) = strokes.pop() {
                let current_state = strokes.clone();
                drop(strokes);
                self.undo_stack.borrow_mut().push(current_state);
                self.redo_stack.borrow_mut().push(vec![last_stroke]);
                self.invalidate_strokes_cache();
                self.update_undo_redo_buttons();
                self.update_placeholder_visibility();
                self.drawing_area.queue_draw();
            }
        }

        // Redo last undone stroke
        fn redo(&self) {
            let strokes_to_redo = self.redo_stack.borrow_mut().pop();
            if let Some(strokes_to_redo) = strokes_to_redo {
                let strokes = self.strokes.borrow_mut();
                let current_state = strokes.clone();
                drop(strokes);
                self.undo_stack.borrow_mut().push(current_state);
                self.strokes.borrow_mut().extend(strokes_to_redo);
                self.invalidate_strokes_cache();
                self.update_undo_redo_buttons();
                self.update_placeholder_visibility();
                self.drawing_area.queue_draw();
            }
        }

        // Update undo/redo button sensitivity
        fn update_undo_redo_buttons(&self) {
            let can_undo = !self.strokes.borrow().is_empty();
            let can_redo = !self.redo_stack.borrow().is_empty();

            self.undo_button.set_sensitive(can_undo);
            self.redo_button.set_sensitive(can_redo);
        }

        // Update placeholder label visibility
        fn update_placeholder_visibility(&self) {
            let has_content =
                !self.strokes.borrow().is_empty() || self.background_pixbuf.borrow().is_some();
            let is_drawing = self.current_stroke.borrow().is_some();

            self.placeholder_label
                .set_visible(!has_content && !is_drawing);
        }

        // Helper to setup cairo drawing style
        fn setup_cairo_style(&self, cr: &cairo::Context, is_dark: bool) {
            if is_dark {
                cr.set_source_rgb(1.0, 1.0, 1.0);
            } else {
                cr.set_source_rgb(0.0, 0.0, 0.0);
            }

            cr.set_line_cap(cairo::LineCap::Round);
            cr.set_line_join(cairo::LineJoin::Round);
        }

        // Helper to render a single stroke
        fn render_stroke(cr: &cairo::Context, stroke: &Stroke) {
            if stroke.points.is_empty() {
                return;
            }

            cr.set_line_width(stroke.width);

            for (i, (x, y)) in stroke.points.iter().enumerate() {
                if i == 0 {
                    cr.move_to(*x, *y);
                } else {
                    cr.line_to(*x, *y);
                }
            }
            let _ = cr.stroke();
        }

        // Invert non-transparent pixels for dark mode display
        fn make_dark_pixbuf(pixbuf: &gdk_pixbuf::Pixbuf) -> Option<gdk_pixbuf::Pixbuf> {
            let copy = pixbuf.copy()?;
            let n_channels = copy.n_channels() as usize;
            let rowstride = copy.rowstride() as usize;
            let width = copy.width() as usize;
            let height = copy.height() as usize;
            let has_alpha = copy.has_alpha();
            let pixels = unsafe { copy.pixels() };
            for y in 0..height {
                for x in 0..width {
                    let i = y * rowstride + x * n_channels;
                    if !has_alpha || pixels[i + 3] > 0 {
                        pixels[i] = 255 - pixels[i];
                        pixels[i + 1] = 255 - pixels[i + 1];
                        pixels[i + 2] = 255 - pixels[i + 2];
                    }
                }
            }
            Some(copy)
        }

        fn invalidate_strokes_cache(&self) {
            self.strokes_cache.replace(None);
        }

        // Draw background pixbuf scaled to fit the canvas (shared by draw_signature and cache build)
        fn render_background(&self, cr: &cairo::Context, width: i32, height: i32, is_dark: bool) {
            let pixbuf = if is_dark {
                self.dark_background_pixbuf.borrow().clone()
            } else {
                self.background_pixbuf.borrow().clone()
            };

            if let Some(pixbuf) = &pixbuf {
                let img_width = pixbuf.width() as f64;
                let img_height = pixbuf.height() as f64;
                let canvas_width = width as f64;
                let canvas_height = height as f64;

                let scale = (canvas_width / img_width)
                    .min(canvas_height / img_height)
                    .min(1.0);

                let scaled_width = img_width * scale;
                let scaled_height = img_height * scale;
                let x_offset = (canvas_width - scaled_width) / 2.0;
                let y_offset = (canvas_height - scaled_height) / 2.0;

                cr.save().unwrap();
                cr.translate(x_offset, y_offset);
                cr.scale(scale, scale);
                cr.set_source_pixbuf(pixbuf, 0.0, 0.0);
                let _ = cr.paint();
                cr.restore().unwrap();
            }
        }

        // Draw the signature on the canvas
        fn draw_signature(&self, cr: &cairo::Context, width: i32, height: i32) {
            cr.set_operator(cairo::Operator::Clear);
            let _ = cr.paint();
            cr.set_operator(cairo::Operator::Over);

            let display = self.drawing_area.display();
            let is_dark = adw::StyleManager::for_display(&display).is_dark();

            // Rebuild cache if missing or canvas was resized
            let cache_valid = self
                .strokes_cache
                .borrow()
                .as_ref()
                .map(|s| s.width() == width && s.height() == height)
                .unwrap_or(false);

            if !cache_valid
                && let Ok(surface) =
                    cairo::ImageSurface::create(cairo::Format::ARgb32, width, height)
            {
                if let Ok(cache_cr) = cairo::Context::new(&surface) {
                    self.render_background(&cache_cr, width, height, is_dark);
                    self.setup_cairo_style(&cache_cr, is_dark);
                    for stroke in &*self.strokes.borrow() {
                        Self::render_stroke(&cache_cr, stroke);
                    }
                }
                self.strokes_cache.replace(Some(surface));
            }

            // Blit cached completed strokes + background
            if let Some(surface) = self.strokes_cache.borrow().as_ref() {
                let _ = cr.set_source_surface(surface, 0.0, 0.0);
                let _ = cr.paint();
            }

            // Draw only the live current stroke on top
            self.setup_cairo_style(cr, is_dark);
            if let Some(stroke) = &*self.current_stroke.borrow() {
                Self::render_stroke(cr, stroke);
            }
        }

        // Handle drag begin event
        fn on_drag_begin(&self, x: f64, y: f64) {
            self.obj().grab_focus();
            self.drag_start.set(Some((x, y)));

            *self.current_stroke.borrow_mut() = Some(Stroke {
                points: vec![(x, y)],
                width: self.pen_width.get(),
            });

            self.update_placeholder_visibility();
            self.drawing_area.queue_draw();
        }

        // Handle drag update event
        fn on_drag_update(&self, offset_x: f64, offset_y: f64) {
            if let Some(stroke) = &mut *self.current_stroke.borrow_mut()
                && let Some((start_x, start_y)) = self.drag_start.get()
            {
                let x = start_x + offset_x;
                let y = start_y + offset_y;
                stroke.points.push((x, y));
                self.drawing_area.queue_draw();
            }
        }

        // Handle drag end event
        fn on_drag_end(&self) {
            self.drag_start.set(None);

            if let Some(stroke) = self.current_stroke.borrow_mut().take() {
                let smoothed_stroke = self.smooth_stroke(stroke);
                self.strokes.borrow_mut().push(smoothed_stroke);
                self.redo_stack.borrow_mut().clear();
            }

            self.invalidate_strokes_cache();
            self.update_undo_redo_buttons();
            self.update_placeholder_visibility();
            self.drawing_area.queue_draw();

            self.obj().emit_by_name::<()>("stroke-completed", &[]);
        }

        // Smooth a stroke using ink-stroke-modeler
        fn smooth_stroke(&self, stroke: Stroke) -> Stroke {
            let smoothed_points = match ink_transformation::smooth_stroke_points(
                stroke.points.clone(),
                None,
                ModelerParams::suggested(),
                1.0,
            ) {
                Ok(points) => points,
                Err(e) => {
                    log::warn!("Failed to smooth stroke: {}, using original", e);
                    stroke.points
                }
            };

            Stroke {
                points: smoothed_points,
                width: stroke.width,
            }
        }

        // Handle file chosen from file dialog
        fn handle_file_chosen(&self, result: Result<gio::File, glib::Error>) {
            match result {
                Ok(file) => {
                    log::debug!("File chosen: {:?}", file.path());

                    let Some(path) = file.path() else {
                        log::error!("File has no path");
                        self.show_error("Failed to get file path");
                        self.importing.set(false);
                        return;
                    };

                    let pixbuf = match gdk_pixbuf::Pixbuf::from_file(&path) {
                        Ok(p) => p,
                        Err(e) => {
                            log::error!("Failed to load image: {}", e);
                            self.show_error(&format!("Failed to load image: {}", e));
                            self.importing.set(false);
                            return;
                        }
                    };

                    const MAX_IMPORT_DIMENSION: f64 = 500.0;
                    let (w, h) = (pixbuf.width() as f64, pixbuf.height() as f64);
                    let scale = (MAX_IMPORT_DIMENSION / w)
                        .min(MAX_IMPORT_DIMENSION / h)
                        .min(1.0);
                    let pixbuf = if scale < 1.0 {
                        pixbuf
                            .scale_simple(
                                (w * scale) as i32,
                                (h * scale) as i32,
                                gdk_pixbuf::InterpType::Bilinear,
                            )
                            .unwrap_or(pixbuf)
                    } else {
                        pixbuf
                    };

                    log::debug!("File loaded, processing signature image in background...");

                    let raw_data = signature_image_processing::RawImageData::from_pixbuf(&pixbuf);

                    let widget = self.obj().clone();
                    glib::spawn_future_local(async move {
                        let result =
                            gio::spawn_blocking(move || raw_data.do_signature_threshold()).await;

                        let imp = widget.imp();

                        imp.importing.set(false);

                        match result {
                            Ok(Ok(raw_signature_image)) => {
                                let processed_pixbuf = raw_signature_image.create_pixbuf();
                                *imp.dark_background_pixbuf.borrow_mut() =
                                    Self::make_dark_pixbuf(&processed_pixbuf);
                                *imp.background_pixbuf.borrow_mut() = Some(processed_pixbuf);

                                log::debug!("File uploaded and processed successfully");

                                imp.invalidate_strokes_cache();
                                imp.update_placeholder_visibility();
                                imp.drawing_area.queue_draw();

                                widget.emit_by_name::<()>("upload-completed", &[]);
                            }
                            Ok(Err(e)) => {
                                log::error!("Failed to process image: {}", e);
                                imp.show_error(&format!("Failed to process image: {}", e));
                            }
                            Err(e) => {
                                log::error!("Background processing failed: {:?}", e);
                                imp.show_error("Image processing failed");
                            }
                        }
                    });
                }
                Err(e) => {
                    log::debug!("File dialog cancelled or error: {}", e);
                    self.importing.set(false);
                }
            }
        }

        // FIXME: this should not be async
        // Get a pixbuf of the current signature, compositing strokes over the background.
        // Returns an error if there is no content.
        pub(super) fn get_pixbuf(&self) -> Result<gdk_pixbuf::Pixbuf, glib::Error> {
            let strokes = self.strokes.borrow();
            let pixbuf_opt = self.background_pixbuf.borrow();

            if strokes.is_empty() && pixbuf_opt.is_none() {
                return Err(glib::Error::new(
                    glib::FileError::Failed,
                    "No signature created",
                ));
            }

            // Background only: return as it is(no re-encoding needed)
            if strokes.is_empty() {
                return Ok(pixbuf_opt.as_ref().unwrap().clone());
            }

            const RENDER_SCALE: i32 = 3;
            const CROP_PADDING: f64 = 4.0;
            const MIN_SIGNATURE_HEIGHT: f64 = 40.0;

            let canvas_w = self.drawing_area.width();
            let canvas_h = self.drawing_area.height();

            if canvas_w <= 0 || canvas_h <= 0 {
                return Err(glib::Error::new(
                    glib::FileError::Failed,
                    "Drawing area not realized",
                ));
            }

            let background_layout = pixbuf_opt.as_ref().map(|pb| {
                let img_w = pb.width() as f64;
                let img_h = pb.height() as f64;
                let cw = canvas_w as f64;
                let ch = canvas_h as f64;
                let scale = (cw / img_w).min(ch / img_h).min(1.0);
                let x_off = (cw - img_w * scale) / 2.0;
                let y_off = (ch - img_h * scale) / 2.0;
                (x_off, y_off, scale)
            });

            let mut min_x = f64::INFINITY;
            let mut min_y = f64::INFINITY;
            let mut max_x = f64::NEG_INFINITY;
            let mut max_y = f64::NEG_INFINITY;

            if let Some((x_off, y_off, scale)) = background_layout {
                let pb = pixbuf_opt.as_ref().unwrap();
                min_x = min_x.min(x_off);
                min_y = min_y.min(y_off);
                max_x = max_x.max(x_off + pb.width() as f64 * scale);
                max_y = max_y.max(y_off + pb.height() as f64 * scale);
            }

            for stroke in strokes.iter() {
                let half_width = stroke.width / 2.0;
                for (x, y) in &stroke.points {
                    min_x = min_x.min(x - half_width);
                    min_y = min_y.min(y - half_width);
                    max_x = max_x.max(x + half_width);
                    max_y = max_y.max(y + half_width);
                }
            }

            min_x -= CROP_PADDING;
            min_y -= CROP_PADDING;
            max_x += CROP_PADDING;
            max_y += CROP_PADDING;

            if max_y - min_y < MIN_SIGNATURE_HEIGHT {
                let extra = (MIN_SIGNATURE_HEIGHT - (max_y - min_y)) / 2.0;
                min_y -= extra;
                max_y += extra;
            }

            let surface_w = ((max_x - min_x).ceil() as i32).max(1);
            let surface_h = ((max_y - min_y).ceil() as i32).max(1);
            let translate_x = -min_x;
            let translate_y = -min_y;

            let surface = cairo::ImageSurface::create(
                cairo::Format::ARgb32,
                surface_w * RENDER_SCALE,
                surface_h * RENDER_SCALE,
            )
            .map_err(|e| {
                glib::Error::new(
                    glib::FileError::Failed,
                    &format!("Failed to create surface: {}", e),
                )
            })?;

            let cr = cairo::Context::new(&surface).map_err(|e| {
                glib::Error::new(
                    glib::FileError::Failed,
                    &format!("Failed to create context: {}", e),
                )
            })?;

            cr.scale(RENDER_SCALE as f64, RENDER_SCALE as f64);

            cr.set_operator(cairo::Operator::Clear);
            let _ = cr.paint();
            cr.set_operator(cairo::Operator::Over);

            cr.translate(translate_x, translate_y);

            if let Some((x_off, y_off, scale)) = background_layout {
                let pb = pixbuf_opt.as_ref().unwrap();
                cr.save().unwrap();
                cr.translate(x_off, y_off);
                cr.scale(scale, scale);
                cr.set_source_pixbuf(pb, 0.0, 0.0);
                let _ = cr.paint();
                cr.restore().unwrap();
            }

            cr.set_source_rgb(0.0, 0.0, 0.0);
            cr.set_line_cap(cairo::LineCap::Round);
            cr.set_line_join(cairo::LineJoin::Round);
            for stroke in strokes.iter() {
                Self::render_stroke(&cr, stroke);
            }

            #[allow(deprecated)]
            let pixbuf = gdk::pixbuf_get_from_surface(
                &surface,
                0,
                0,
                surface_w * RENDER_SCALE,
                surface_h * RENDER_SCALE,
            )
            .ok_or_else(|| {
                glib::Error::new(glib::FileError::Failed, "Failed to get pixbuf from surface")
            })?;

            Ok(pixbuf)
        }

        // Reset the widget
        pub(super) fn reset(&self) {
            self.strokes.borrow_mut().clear();
            *self.background_pixbuf.borrow_mut() = None;
            *self.dark_background_pixbuf.borrow_mut() = None;
            *self.current_stroke.borrow_mut() = None;

            self.undo_stack.borrow_mut().clear();
            self.redo_stack.borrow_mut().clear();
            self.invalidate_strokes_cache();
            self.update_undo_redo_buttons();
            self.update_placeholder_visibility();

            self.drawing_area.queue_draw();
        }

        // Load existing signature for editing (sets background pixbuf, preserves strokes)
        pub(super) fn load_signature(&self, pixbuf: gdk_pixbuf::Pixbuf) -> Result<(), String> {
            *self.dark_background_pixbuf.borrow_mut() = Self::make_dark_pixbuf(&pixbuf);
            *self.background_pixbuf.borrow_mut() = Some(pixbuf);

            self.invalidate_strokes_cache();
            self.update_placeholder_visibility();
            self.drawing_area.queue_draw();

            Ok(())
        }

        pub(super) fn has_signature(&self) -> bool {
            !self.strokes.borrow().is_empty() || self.background_pixbuf.borrow().is_some()
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
    }
}

glib::wrapper! {
    pub struct PpsSignatureDrawingWidget(ObjectSubclass<imp::PpsSignatureDrawingWidget>)
        @extends gtk::Box, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Orientable;
}

impl Default for PpsSignatureDrawingWidget {
    fn default() -> Self {
        Self::new()
    }
}

impl PpsSignatureDrawingWidget {
    pub fn new() -> Self {
        glib::Object::builder().build()
    }

    pub fn get_pixbuf(&self) -> Result<gdk_pixbuf::Pixbuf, glib::Error> {
        self.imp().get_pixbuf()
    }

    pub fn reset(&self) {
        self.imp().reset();
    }

    pub fn load_signature(&self, pixbuf: gdk_pixbuf::Pixbuf) -> Result<(), String> {
        self.imp().load_signature(pixbuf)
    }

    pub fn has_signature(&self) -> bool {
        self.imp().has_signature()
    }

    pub fn insert_button(&self) -> gtk::Button {
        self.imp().insert_button.get()
    }
}
