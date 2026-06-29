// ================================================================
//  Smart Shelter — Dog House 3D Model
//  Units: mm  |  Scale 1:1
// ================================================================

// ── DIMENSIONS ──────────────────────────────────────────────────
W      = 500;        // total width
D      = 500;        // depth (front to back)
H_leg  = 80;         // leg height
leg_s  = 30;         // leg cross-section
t      = 20;         // wall / plank thickness
H_wall = 470;        // wall height (floor → eave)
H_peak = 145;        // gable peak above eave

// Entrance
ent_w  = 200;        // width = 20cm
ent_r  = ent_w / 2; // arch radius = 100
ent_hs = 300 - ent_r; // straight part so total height = 300mm (30cm)
ent_x  = 50;         // 5cm from left wall

// Sensor hole (VL53L1X — Ø10mm, 150mm above arch top)
sens_r = 5;
sens_z = ent_hs + ent_r + 150 + sens_r; // z from floor

// Cable hole — Ø10mm, near bottom of front wall
cable_r = 5;
cable_z = 30; // 30mm above floor

// Roof overhangs
ov_f = 200;  // front
ov_b =  60;  // back
ov_s =  40;  // each side

// Colors
C_WOOD = [0.72, 0.52, 0.30];
C_DARK = [0.40, 0.24, 0.10];
C_FELT = [0.20, 0.18, 0.18];

$fn = 64;

// ── MODULES ─────────────────────────────────────────────────────

module leg() {
    color(C_DARK) cube([leg_s, leg_s, H_leg]);
}

module front_wall() {
    color(C_WOOD)
    difference() {
        cube([W, t, H_wall]);
        // entrance arch cutout — 5cm from left, 25cm from right
        translate([ent_x, -1, 0]) {
            cube([ent_w, t+2, ent_hs]);
            translate([ent_r, 0, ent_hs])
                rotate([-90, 0, 0])
                    cylinder(h=t+2, r=ent_r);
        }
        // sensor hole (centered on entrance)
        translate([ent_x + ent_r, -1, sens_z])
            rotate([-90, 0, 0])
                cylinder(h=t+2, r=sens_r);
        // cable hole (bottom, same x center)
        translate([ent_x + ent_r, -1, cable_z])
            rotate([-90, 0, 0])
                cylinder(h=t+2, r=cable_r);
    }
}

module back_wall() {
    color(C_WOOD) cube([W, t, H_wall]);
}

module side_wall(depth) {
    color(C_WOOD) cube([t, depth, H_wall]);
}

module floor_board() {
    color(C_WOOD) cube([W - 2*t, D - 2*t, t]);
}

module roof() {
    rw = W + 2*ov_s;
    rd = D + ov_f + ov_b;
    color(C_FELT)
    translate([-ov_s, -ov_f, 0])
    polyhedron(
        points = [
            [0,    0,  0],        // 0 front-left
            [rw,   0,  0],        // 1 front-right
            [rw/2, 0,  H_peak],   // 2 front-peak
            [0,    rd, 0],        // 3 back-left
            [rw,   rd, 0],        // 4 back-right
            [rw/2, rd, H_peak]    // 5 back-peak
        ],
        faces = [
            [0,2,1],      // front gable
            [3,4,5],      // back gable
            [0,1,4,3],    // bottom (eave line)
            [1,2,5,4],    // right slope
            [0,3,5,2]     // left slope
        ]
    );
}

module fascia_boards() {
    color(C_DARK) {
        translate([-ov_s, -ov_f, -t])
            cube([W + 2*ov_s, t, t]);  // front eave board
        translate([-ov_s, D + ov_b - t, -t])
            cube([W + 2*ov_s, t, t]);  // back eave board
    }
}

// ── ASSEMBLY ────────────────────────────────────────────────────

// 4 corner legs
for (lx = [0, W - leg_s], ly = [0, D - leg_s])
    translate([lx, ly, 0]) leg();

// Floor
translate([t, t, H_leg]) floor_board();

// Walls (placed on top of legs)
translate([0, 0, H_leg]) {
    front_wall();                              // y = 0
    translate([0, D-t, 0]) back_wall();       // y = D-t
    translate([0,   t, 0]) side_wall(D-2*t); // left
    translate([W-t, t, 0]) side_wall(D-2*t); // right
}

// Roof + fascia at eave level
translate([0, 0, H_leg + H_wall]) {
    roof();
    fascia_boards();
}
