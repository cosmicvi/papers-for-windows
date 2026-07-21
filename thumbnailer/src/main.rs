// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright © 2014 FineFindus
use std::process::ExitCode;

use gio::prelude::FileExt;
use image::{ImageBuffer, ImageFormat, Rgb, Rgba};
use papers_document::prelude::DocumentExt;
use papers_document::RenderAnnotsFlags;

use gettextrs::{setlocale, LocaleCategory};

const USAGE: &str = "Usage:
  papers-thumbnailer [OPTION…] <input> <output> - GNOME Document Thumbnailer

Help Options:
  -h, --help              Show help options

Application Options:
  -s, --size=SIZE
";

const THUMBNAIL_SIZE: usize = 128;

struct Args {
    input: String,
    thumbnail_path: String,
    size: usize,
}

impl Args {
    fn new() -> Option<Args> {
        let mut args = std::env::args().skip(1);

        let mut input = None;
        let mut thumbnail_path = None;
        let mut size = THUMBNAIL_SIZE;

        while let Some(arg) = args.next() {
            match arg.as_ref() {
                // return None to trigger usage dialog
                "-h" | "--help" => return None,
                "-s" | "--size" => size = args.next()?.parse::<usize>().unwrap_or(THUMBNAIL_SIZE),
                arg if arg.starts_with("--size=") => {
                    size = arg
                        .strip_prefix("--size=")?
                        .parse::<usize>()
                        .unwrap_or(THUMBNAIL_SIZE)
                }
                _v if input.is_none() => input = Some(arg),
                _v if thumbnail_path.is_none() => thumbnail_path = Some(arg),
                _ => {}
            };
        }

        Some(Args {
            size,
            input: input?,
            thumbnail_path: thumbnail_path?,
        })
    }
}

fn main() -> ExitCode {
    env_logger::builder().format_timestamp_millis().init();

    setlocale(LocaleCategory::LcAll, "");

    let Some(args) = Args::new() else {
        println!("{}", USAGE);
        return ExitCode::FAILURE;
    };

    if !papers_document::init() {
        return ExitCode::FAILURE;
    }

    // open document from uri
    let file = gio::File::for_commandline_arg(&args.input);
    let Some(uri) = absolute_uri(&file) else {
        log::error!("Failed to get uri for {:?}", args.input);
        return ExitCode::FAILURE;
    };

    let Ok(document) = papers_document::Document::factory_get_document(&uri) else {
        papers_document::shutdown();
        log::error!("Failed to get document for {uri}");
        return ExitCode::FAILURE;
    };
    if let Err(err) = papers_document::Document::load(&document, &uri) {
        papers_document::shutdown();
        log::error!("Failed to load document for {uri}: {err}");
        return ExitCode::FAILURE;
    }

    // start a timer
    std::thread::spawn(move || {
        std::thread::sleep(std::time::Duration::from_secs(15));
        // the timer will be killed, when the process finishes before the timer
        // so we do not need to handling the happy path of the document rendering in < 15s
        log::error!("Couldn't process file: {uri}, took too much time.");
        std::process::exit(1);
    });

    if render_thumbnail_to_file(&document, &args.thumbnail_path, args.size as f64).is_none() {
        papers_document::shutdown();
        return ExitCode::FAILURE;
    }

    papers_document::shutdown();
    ExitCode::SUCCESS
}

fn absolute_uri(file: &gio::File) -> Option<glib::GString> {
    if !file.has_uri_scheme("trash") && !file.has_uri_scheme("recent") {
        return Some(file.uri());
    }

    let info = file
        .query_info(
            gio::FILE_ATTRIBUTE_STANDARD_TARGET_URI,
            gio::FileQueryInfoFlags::NONE,
            gio::Cancellable::NONE,
        )
        .ok()?;
    info.attribute_string(gio::FILE_ATTRIBUTE_STANDARD_TARGET_URI)
}

/// Render the first page of `document` as a thumbnail to `thumbnail_path`.
fn render_thumbnail_to_file(
    document: &papers_document::Document,
    thumbnail_path: &str,
    size: f64,
) -> Option<()> {
    let page = document.page(0)?;
    let (width, height) = document.page_size_uncached(&page);

    let render_ctxt = papers_document::RenderContext::new(
        &page,
        0,
        size / height.max(width),
        RenderAnnotsFlags::ALL,
    );
    let pixbuf = document.thumbnail(&render_ctxt)?;
    let data = pixbuf.read_pixel_bytes().to_vec();
    let stride = pixbuf.rowstride() as u32;
    let n_channels = pixbuf.n_channels() as u32;
    let (width, height) = (pixbuf.width() as u32, pixbuf.height() as u32);

    let pixel = |x, y| {
        &data[(y * stride + x * n_channels) as usize..(y * stride + (x + 1) * n_channels) as usize]
    };

    // As in a pixbuf the stride may be different from width * n_channels,
    // we cannot use image::ImageBuffer::from_vec directly and we have to
    // enumerate all pixels
    // Since this is statically typed, it is necessary to have two copies
    // of this code: one for 3-channels pixbuf and another one for 4-channels one
    match n_channels {
        4 => ImageBuffer::from_fn(width, height, |x, y| Rgba(pixel(x, y).try_into().unwrap()))
            .save_with_format(thumbnail_path, ImageFormat::Png)
            .ok(),
        3 => ImageBuffer::from_fn(width, height, |x, y| Rgb(pixel(x, y).try_into().unwrap()))
            .save_with_format(thumbnail_path, ImageFormat::Png)
            .ok(),
        _ => None,
    }
}
