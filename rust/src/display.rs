//! Drawing a cube for a terminal.
//!
//! Deliberately separate from [`crate::cube`]: the model is 24 bytes and knows
//! nothing about how anyone looks at it.
//!
//! The picture is the standard unfolded net — UP above FRONT, the four side
//! faces in a band, DOWN below — so it matches the order a state string is
//! written in. BACK is drawn as seen from behind, which is what makes the band
//! read as one continuous strip around the cube.

use crate::cube::{BACK, DOWN, FRONT, LEFT, RIGHT, State, UP, color_char};

const CELL: usize = 4;
const FACE: usize = CELL * 2;
const GAP: &str = "  ";

/// The side faces, left to right.
const BAND: [(&str, usize); 4] =
    [("LEFT", LEFT), ("FRONT", FRONT), ("RIGHT", RIGHT), ("BACK", BACK)];

fn centered(text: &str, width: usize) -> String {
    let left = (width - text.len()) / 2;
    format!("{:left$}{text}{:right$}", "", "", right = width - text.len() - left)
}

/// The two text rows of one face.
fn face(state: &State, side: usize) -> [String; 2] {
    // BACK is viewed from behind, so its stickers read in reverse.
    let order = if side == BACK { [[3, 2], [1, 0]] } else { [[0, 1], [2, 3]] };
    order.map(|row| {
        row.iter()
            .map(|&sticker| {
                let letter = color_char(state[side * 4 + sticker]).to_ascii_uppercase();
                centered(&letter.to_string(), CELL)
            })
            .collect()
    })
}

/// Draw the cube as an unfolded net of colour letters.
pub fn render(state: &State) -> String {
    let indent = " ".repeat(FACE + GAP.len());
    let mut lines = vec![format!("{indent}{}", centered("UP", FACE))];
    lines.extend(face(state, UP).map(|row| format!("{indent}{row}")));

    lines.push(String::new());
    let band: Vec<[String; 2]> = BAND.iter().map(|&(_, side)| face(state, side)).collect();
    lines.push(BAND.iter().map(|&(name, _)| centered(name, FACE)).collect::<Vec<_>>().join(GAP));
    for row in 0..2 {
        lines.push(band.iter().map(|face| face[row].clone()).collect::<Vec<_>>().join(GAP));
    }

    lines.push(String::new());
    lines.push(format!("{indent}{}", centered("DOWN", FACE)));
    lines.extend(face(state, DOWN).map(|row| format!("{indent}{row}")));
    lines.join("\n")
}
