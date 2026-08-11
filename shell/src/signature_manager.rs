use crate::deps::*;
use gdk::gdk_pixbuf;
use serde::{Deserialize, Serialize};
use std::cell::Cell;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::OnceLock;

/// Metadata for a single signature
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignatureMetadata {
    pub id: String,
    pub name: String,
    pub filename: String,
    pub creation_date: String, // ISO 8601 format
    pub width: i32,
    pub height: i32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pending_deletion: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub deletion_time: Option<String>,
}

/// Root metadata structure for the JSON file
#[derive(Debug, Clone, Serialize, Deserialize)]
struct MetadataFile {
    version: u32,
    signatures: Vec<SignatureMetadata>,
}

impl Default for MetadataFile {
    fn default() -> Self {
        Self {
            version: 1,
            signatures: Vec::new(),
        }
    }
}

mod imp {
    use super::*;

    #[derive(Debug, Default)]
    pub struct PpsSignatureManager {
        pub(super) metadata: RefCell<Option<MetadataFile>>,
        pub(super) file_monitor: RefCell<Option<gio::FileMonitor>>,
        pub(super) pending_saves: Cell<u32>,
        pub(super) pixbuf_cache: RefCell<HashMap<String, gdk_pixbuf::Pixbuf>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for PpsSignatureManager {
        const NAME: &'static str = "PpsSignatureManager";
        type Type = super::PpsSignatureManager;
        type ParentType = glib::Object;
    }

    impl ObjectImpl for PpsSignatureManager {
        fn signals() -> &'static [glib::subclass::Signal] {
            static SIGNALS: OnceLock<Vec<glib::subclass::Signal>> = OnceLock::new();
            SIGNALS.get_or_init(|| {
                vec![glib::subclass::Signal::builder("signatures-list-changed").build()]
            })
        }

        fn constructed(&self) {
            self.parent_constructed();

            glib::spawn_future_local(async move {
                let dir = PpsSignatureManager::get_signatures_dir();
                match gio::spawn_blocking(move || std::fs::create_dir_all(dir)).await {
                    Ok(Ok(())) => {}
                    Ok(Err(e)) => log::error!("Failed to create signatures directory: {}", e),
                    Err(_) => log::error!("Failed to create signatures directory"),
                }
            });

            let metadata_file = gio::File::for_path(Self::get_metadata_path());
            match metadata_file.monitor_file(gio::FileMonitorFlags::NONE, gio::Cancellable::NONE) {
                Ok(monitor) => {
                    let obj = self.obj().downgrade();
                    monitor.connect_changed(move |_, _, _, event| {
                        let Some(manager) = obj.upgrade() else {
                            return;
                        };
                        let imp = manager.imp();
                        match event {
                            gio::FileMonitorEvent::Created | gio::FileMonitorEvent::Changed => {
                                if imp.pending_saves.get() > 0 {
                                    return;
                                }
                                *imp.metadata.borrow_mut() = None;
                                manager.emit_by_name::<()>("signatures-list-changed", &[]);
                            }
                            gio::FileMonitorEvent::ChangesDoneHint => {
                                let n = imp.pending_saves.get();
                                if n > 0 {
                                    imp.pending_saves.set(n - 1);
                                    return;
                                }
                                *imp.metadata.borrow_mut() = None;
                                manager.emit_by_name::<()>("signatures-list-changed", &[]);
                            }
                            _ => {}
                        }
                    });
                    *self.file_monitor.borrow_mut() = Some(monitor);
                }
                Err(e) => log::error!("Failed to monitor metadata file: {}", e),
            }
        }
    }

    impl PpsSignatureManager {
        /// Get the signatures directory path
        pub(super) fn get_signatures_dir() -> PathBuf {
            let mut path = glib::user_data_dir();
            path.push("papers");
            path.push("signatures");
            path
        }

        fn get_metadata_path() -> PathBuf {
            Self::get_signatures_dir().join("metadata.json")
        }

        // Compute the path for a signature image purely from its ID
        fn signature_path_for_id(id: &str) -> PathBuf {
            Self::get_signatures_dir().join(format!("signature_{}.png", id))
        }

        // Load metadata from disk if not already cached
        pub(super) async fn ensure_metadata_loaded(&self) -> Result<(), glib::Error> {
            if self.metadata.borrow().is_some() {
                return Ok(());
            }

            let file = gio::File::for_path(Self::get_metadata_path());

            let metadata = match file.load_contents_future().await {
                Ok((bytes, _)) => serde_json::from_slice::<MetadataFile>(&bytes).map_err(|e| {
                    glib::Error::new(
                        glib::FileError::Failed,
                        &format!("Failed to parse metadata.json: {}", e),
                    )
                })?,
                Err(e) if e.kind::<gio::IOErrorEnum>() == Some(gio::IOErrorEnum::NotFound) => {
                    let default = MetadataFile::default();
                    Self::write_metadata_async(&default).await?;
                    default
                }
                Err(e) => return Err(e),
            };

            *self.metadata.borrow_mut() = Some(metadata);
            Ok(())
        }

        // Save metadata to disk and update the in-memory cache
        pub(super) async fn save_metadata(
            &self,
            metadata: MetadataFile,
        ) -> Result<(), glib::Error> {
            self.pending_saves.set(self.pending_saves.get() + 1);
            match Self::write_metadata_async(&metadata).await {
                Ok(()) => {
                    *self.metadata.borrow_mut() = Some(metadata);
                    Ok(())
                }
                Err(e) => {
                    // Write failed — no monitor event will arrive, so decrement now.
                    let n = self.pending_saves.get();
                    if n > 0 {
                        self.pending_saves.set(n - 1);
                    }
                    Err(e)
                }
            }
        }

        // Write metadata to disk asynchronously
        async fn write_metadata_async(metadata: &MetadataFile) -> Result<(), glib::Error> {
            let content = serde_json::to_string_pretty(metadata).map_err(|e| {
                glib::Error::new(
                    glib::FileError::Failed,
                    &format!("Failed to serialize metadata: {}", e),
                )
            })?;

            let file = gio::File::for_path(Self::get_metadata_path());
            file.replace_contents_future(
                content.into_bytes(),
                None,
                false,
                gio::FileCreateFlags::REPLACE_DESTINATION,
            )
            .await
            .map_err(|(_, e)| e)?;

            Ok(())
        }

        // Construct a new SignatureMetadata entry
        fn make_entry(id: &str, name: &str, pixbuf: &gdk_pixbuf::Pixbuf) -> SignatureMetadata {
            SignatureMetadata {
                id: id.to_string(),
                name: name.to_string(),
                filename: format!("signature_{}.png", id),
                creation_date: Self::get_current_timestamp(),
                width: pixbuf.width(),
                height: pixbuf.height(),
                pending_deletion: None,
                deletion_time: None,
            }
        }

        // Generate unique ID for a new signature (timestamp-based)
        pub(super) fn generate_id() -> String {
            use std::time::{SystemTime, UNIX_EPOCH};
            let timestamp = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("System time is before UNIX epoch")
                .as_secs();
            format!("sig_{}", timestamp)
        }

        /// Get current timestamp in ISO 8601 format
        pub(super) fn get_current_timestamp() -> String {
            use chrono::Utc;
            Utc::now().to_rfc3339()
        }

        pub(super) async fn list_signatures(&self) -> Vec<SignatureMetadata> {
            if let Err(e) = self.ensure_metadata_loaded().await {
                log::error!("Failed to load signatures: {}", e);
                return Vec::new();
            }

            self.metadata
                .borrow()
                .as_ref()
                .map(|m| {
                    m.signatures
                        .iter()
                        .filter(|s| !s.pending_deletion.unwrap_or(false))
                        .cloned()
                        .collect()
                })
                .unwrap_or_default()
        }

        pub(super) async fn add_signature(
            &self,
            name: &str,
            pixbuf: &gdk_pixbuf::Pixbuf,
        ) -> Result<String, glib::Error> {
            self.ensure_metadata_loaded().await?;
            let mut metadata = self.metadata.borrow().as_ref().unwrap().clone();

            let id = Self::generate_id();

            let png_data = pixbuf.save_to_bufferv("png", &[]).map_err(|e| {
                glib::Error::new(
                    glib::FileError::Failed,
                    &format!("Failed to encode signature as PNG: {}", e),
                )
            })?;

            gio::File::for_path(Self::signature_path_for_id(&id))
                .replace_contents_future(
                    png_data.to_vec(),
                    None,
                    false,
                    gio::FileCreateFlags::REPLACE_DESTINATION,
                )
                .await
                .map_err(|(_, e)| e)?;

            metadata
                .signatures
                .push(Self::make_entry(&id, name, pixbuf));
            self.save_metadata(metadata).await?;

            self.obj()
                .emit_by_name::<()>("signatures-list-changed", &[]);

            log::info!("Added signature: {} ({})", name, id);

            Ok(id)
        }

        pub(super) async fn get_signature(&self, id: &str) -> Option<SignatureMetadata> {
            self.ensure_metadata_loaded().await.ok();

            self.metadata
                .borrow()
                .as_ref()
                .and_then(|m| m.signatures.iter().find(|s| s.id == id).cloned())
        }

        pub(super) async fn mark_for_deletion(&self, id: &str) -> Result<(), glib::Error> {
            self.ensure_metadata_loaded().await?;
            let mut metadata = self.metadata.borrow().as_ref().unwrap().clone();

            let signature = metadata
                .signatures
                .iter_mut()
                .find(|s| s.id == id)
                .ok_or_else(|| glib::Error::new(glib::FileError::Noent, "Signature not found"))?;

            signature.pending_deletion = Some(true);
            signature.deletion_time = Some(Self::get_current_timestamp());

            self.save_metadata(metadata).await?;

            self.obj()
                .emit_by_name::<()>("signatures-list-changed", &[]);

            log::info!("Marked signature for deletion: {}", id);

            Ok(())
        }

        pub(super) async fn restore_from_deletion(&self, id: &str) -> Result<(), glib::Error> {
            // Verify existence before mutating
            self.get_signature(id)
                .await
                .ok_or_else(|| glib::Error::new(glib::FileError::Noent, "Signature not found"))?;

            // ensure_metadata_loaded is already guaranteed by get_signature above
            let mut metadata = self.metadata.borrow().as_ref().unwrap().clone();

            let signature = metadata.signatures.iter_mut().find(|s| s.id == id).unwrap();

            signature.pending_deletion = None;
            signature.deletion_time = None;

            self.save_metadata(metadata).await?;

            self.obj()
                .emit_by_name::<()>("signatures-list-changed", &[]);

            log::info!("Restored signature from deletion: {}", id);

            Ok(())
        }

        pub(super) async fn permanently_delete(&self, id: &str) -> Result<(), glib::Error> {
            self.ensure_metadata_loaded().await?;
            let mut metadata = self.metadata.borrow().as_ref().unwrap().clone();

            let had_entry = metadata.signatures.iter().any(|s| s.id == id);
            if !had_entry {
                return Err(glib::Error::new(
                    glib::FileError::Noent,
                    "Signature not found",
                ));
            }

            let image_file = gio::File::for_path(Self::signature_path_for_id(id));
            match image_file.delete_future(glib::Priority::DEFAULT).await {
                Ok(_) => {}
                Err(e) if e.kind::<gio::IOErrorEnum>() == Some(gio::IOErrorEnum::NotFound) => {}
                Err(e) => return Err(e),
            }

            self.pixbuf_cache.borrow_mut().remove(id);

            metadata.signatures.retain(|s| s.id != id);
            self.save_metadata(metadata).await?;

            self.obj()
                .emit_by_name::<()>("signatures-list-changed", &[]);

            log::info!("Permanently deleted signature: {}", id);

            Ok(())
        }

        pub(super) async fn get_signature_path(&self, id: &str) -> Option<PathBuf> {
            self.ensure_metadata_loaded().await.ok();
            self.metadata
                .borrow()
                .as_ref()
                .and_then(|m| m.signatures.iter().find(|s| s.id == id))
                .map(|_| Self::signature_path_for_id(id))
        }

        pub(super) fn get_signature_pixbuf(&self, id: &str) -> Option<gdk_pixbuf::Pixbuf> {
            if let Some(pixbuf) = self.pixbuf_cache.borrow().get(id).cloned() {
                return Some(pixbuf);
            }
            let path = Self::signature_path_for_id(id);
            match gdk_pixbuf::Pixbuf::from_file(&path) {
                Ok(pixbuf) => {
                    self.pixbuf_cache
                        .borrow_mut()
                        .insert(id.to_string(), pixbuf.clone());
                    Some(pixbuf)
                }
                Err(e) => {
                    log::warn!("Failed to load signature pixbuf {}: {}", id, e);
                    None
                }
            }
        }

        pub(super) async fn update_signature(
            &self,
            id: &str,
            name: &str,
            pixbuf: &gdk_pixbuf::Pixbuf,
        ) -> Result<(), glib::Error> {
            self.ensure_metadata_loaded().await?;
            let mut metadata = self.metadata.borrow().as_ref().unwrap().clone();

            let signature = metadata
                .signatures
                .iter_mut()
                .find(|s| s.id == id)
                .ok_or_else(|| glib::Error::new(glib::FileError::Noent, "Signature not found"))?;

            signature.name = name.to_string();
            signature.width = pixbuf.width();
            signature.height = pixbuf.height();

            let png_data = pixbuf.save_to_bufferv("png", &[]).map_err(|e| {
                glib::Error::new(
                    glib::FileError::Failed,
                    &format!("Failed to encode signature as PNG: {}", e),
                )
            })?;

            gio::File::for_path(Self::signature_path_for_id(id))
                .replace_contents_future(
                    png_data.to_vec(),
                    None,
                    false,
                    gio::FileCreateFlags::REPLACE_DESTINATION,
                )
                .await
                .map_err(|(_, e)| e)?;

            self.pixbuf_cache.borrow_mut().remove(id);

            self.save_metadata(metadata).await?;

            self.obj()
                .emit_by_name::<()>("signatures-list-changed", &[]);
            log::info!("Updated signature: {}", id);

            Ok(())
        }
    }
}

glib::wrapper! {
    pub struct PpsSignatureManager(ObjectSubclass<imp::PpsSignatureManager>);
}

impl Default for PpsSignatureManager {
    fn default() -> Self {
        Self::new()
    }
}

impl PpsSignatureManager {
    pub fn new() -> Self {
        glib::Object::builder().build()
    }

    pub async fn list_signatures(&self) -> Vec<SignatureMetadata> {
        self.imp().list_signatures().await
    }

    pub async fn add_signature(
        &self,
        name: &str,
        pixbuf: &gdk_pixbuf::Pixbuf,
    ) -> Result<String, glib::Error> {
        self.imp().add_signature(name, pixbuf).await
    }

    pub async fn get_signature(&self, id: &str) -> Option<SignatureMetadata> {
        self.imp().get_signature(id).await
    }

    pub async fn mark_for_deletion(&self, id: &str) -> Result<(), glib::Error> {
        self.imp().mark_for_deletion(id).await
    }

    pub async fn restore_from_deletion(&self, id: &str) -> Result<(), glib::Error> {
        self.imp().restore_from_deletion(id).await
    }

    pub async fn permanently_delete(&self, id: &str) -> Result<(), glib::Error> {
        self.imp().permanently_delete(id).await
    }

    pub async fn get_signature_path(&self, id: &str) -> Option<PathBuf> {
        self.imp().get_signature_path(id).await
    }

    pub fn get_signature_pixbuf(&self, id: &str) -> Option<gdk_pixbuf::Pixbuf> {
        self.imp().get_signature_pixbuf(id)
    }

    pub async fn update_signature(
        &self,
        id: &str,
        name: &str,
        pixbuf: &gdk_pixbuf::Pixbuf,
    ) -> Result<(), glib::Error> {
        self.imp().update_signature(id, name, pixbuf).await
    }
}
