use crate::deps::*;
use ink_stroke_modeler_rs::{ModelerInput, ModelerInputEventType, ModelerParams, StrokeModeler};
use papers_document::{AnnotationInk, InkList, Path, Point};
use papers_view::AnnotationsContext;

// Smooths stroke points using ink-stroke-modeler.
pub fn smooth_stroke_points(
    points: Vec<(f64, f64)>,
    timestamps: Option<Vec<f64>>,
    params: ModelerParams,
    pressure: f64,
) -> Result<Vec<(f64, f64)>, String> {
    if points.len() < 2 {
        return Ok(points);
    }

    let mut modeler = StrokeModeler::new(params)
        .map_err(|e| format!("Failed to create stroke modeler: {}", e))?;

    // Build inputs, initially all set to Move
    let mut input: Vec<ModelerInput> = points
        .iter()
        .enumerate()
        .map(|(i, (x, y))| ModelerInput {
            event_type: ModelerInputEventType::Move,
            pos: (*x, *y),
            time: timestamps
                .as_ref()
                .and_then(|ts| ts.get(i).copied())
                .unwrap_or(i as f64 * 0.01),
            pressure,
        })
        .collect();

    // Set proper event types for first and last points
    let n = input.len();
    if let Some(first) = input.first_mut() {
        first.event_type = ModelerInputEventType::Down;
    }
    if let Some(last) = input.get_mut(n - 1) {
        last.event_type = ModelerInputEventType::Up;
    }

    // Process all inputs and collect smoothed points
    let result_stroke: Vec<(f64, f64)> = input
        .into_iter()
        .filter_map(|i| modeler.update(i).ok())
        .flatten()
        .map(|r| r.pos)
        .collect();

    if result_stroke.is_empty() {
        Ok(points)
    } else {
        Ok(result_stroke)
    }
}

pub fn setup() {
    AnnotationsContext::register_ink_transformation(|a| {
        if let Ok(ink) = a.clone().downcast::<AnnotationInk>() {
            let mut p = ModelerParams::suggested();
            p.sampling_max_outputs_per_call = 200;

            let time_list = ink.time_list();
            let t0 = if let Some(k) = time_list.first() {
                k.time() as i32
            } else {
                return;
            };

            // Extract points and timestamps from time_list
            let points: Vec<(f64, f64)> = time_list.iter().map(|t| (t.x(), t.y())).collect();
            let timestamps: Vec<f64> = time_list
                .iter()
                .map(|t| ((t.time() as i32 - t0) as f64) / 1000.)
                .collect();

            // Smooth the stroke using the shared helper
            let result_stroke = match smooth_stroke_points(points, Some(timestamps), p, 0.25) {
                Ok(smoothed) => smoothed,
                Err(e) => {
                    eprintln!("modeler error: {e}");
                    return;
                }
            };
            let area: (f64, f64, f64, f64) = result_stroke
                .iter()
                .fold(None, |a, p| {
                    let b = a.unwrap_or((p.0, p.0, p.1, p.1));
                    Some((b.0.min(p.0), b.1.max(p.0), b.2.min(p.1), b.3.max(p.1)))
                })
                .unwrap();

            let r = a.border_width();
            let is_point = (area.0 - area.1).powf(2.) + (area.2 - area.3).powf(2.) < r;
            let ip = if is_point {
                let mut p1 = Point::new();
                let mut p2 = Point::new();
                p1.set_x(area.0 - r / 2.);
                p1.set_y(area.2 - r / 2.);
                p2.set_x(area.0 + r / 2.);
                p2.set_y(area.2 + r / 2.);
                Path::for_array(&[p1, p2])
            } else {
                let result_simplified = {
                    let simplified = simplify(result_stroke, 0.05);

                    simplified
                        .into_iter()
                        .map(|r| {
                            let mut p = Point::new();
                            p.set_x(r.0);
                            p.set_y(r.1);
                            p
                        })
                        .collect::<Vec<Point>>()
                };
                Path::for_array(&result_simplified)
            };

            let il = InkList::for_array(&[ip]);
            ink.set_ink_list(il);
        }
    });
}

// Ramer–Douglas-Peucker line decimation algorithm
fn simplify(line: Vec<(f64, f64)>, epsilon: f64) -> Vec<(f64, f64)> {
    let [first, .., last] = line[..] else {
        return line;
    };

    let (maxi, maxd) = line
        .iter()
        .enumerate()
        .take(line.len() - 1)
        .skip(1)
        .map(|(i, p)| (i, euclidean_dist(*p, first, last)))
        .reduce(|acc, other| if other.1 > acc.1 { other } else { acc })
        .unwrap_or((0, 0.));

    if maxd > epsilon {
        let mut res1 = simplify(line[0..=maxi].to_vec(), epsilon);
        let res2 = simplify(line[maxi..line.len()].to_vec(), epsilon);

        res1.pop();
        res1.extend(res2);
        res1
    } else {
        vec![first, last]
    }
}

// Distance between point p and 2 point line (start, end)
fn euclidean_dist(p: (f64, f64), start: (f64, f64), end: (f64, f64)) -> f64 {
    ((end.1 - start.1) * p.0 - (end.0 - start.1) * p.1 + end.0 * start.1 - end.1 * start.0).abs()
        / dist(start, end)
}

// Distance between two points
fn dist(p1: (f64, f64), p2: (f64, f64)) -> f64 {
    ((p1.0 - p2.0).powf(2.) + (p1.0 - p2.0).powf(2.)).sqrt()
}
