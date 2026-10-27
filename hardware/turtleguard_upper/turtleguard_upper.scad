/*
 * TurtleGuard upper enclosure
 *
 * Parametric, support-conscious first prototype for the current NodeMCU sensor
 * station. Units are millimetres. Export the lid, electronics/GPS sleds,
 * sensor cartridges, and lower-base tripod adapter as separate STL files.
 *
 * Coordinate system:
 *   +X = turtle's head/front, +Y = turtle's left side, +Z = up.
 */

// "production" preserves the original smooth 72-segment geometry. Use
// "draft" for faster local fit checks; it does not change any dimensions.
render_quality = "production";
$fn = render_quality == "draft" ? 36 : 72;

// Select: "assembly", "lid", "electronics_sled", "gps_sled",
//         "optical_cartridge", "env_cartridge", or "tripod_adapter".
part = "assembly";

// Overall enclosure envelope. Measure the mating base and adjust these first.
shell_length = 240;
shell_width = 160;
shell_height = 68;
wall = 3.0;
flange_width = 7.0;
flange_height = 3.0;
lip_drop = 3.0;
lip_wall = 2.0;
fit_clearance = 0.35;

// Lid-to-base screws (M3 clearance by default).
lid_screw_d = 3.4;
lid_counterbore_d = 6.5;
lid_fasteners = [[-82, 53], [-82, -53], [82, 53], [82, -53]];

// Power bank cradle. Defaults fit many 10,000 mAh packs; measure yours.
powerbank = [142, 68, 18];
powerbank_origin = [-20, 0];
powerbank_clearance = 0.8;
powerbank_rail_height = 21;
powerbank_strap_width = 14;

// Main electronics tray. Defaults fit a 90 x 60 mm perfboard/PCB.
pcb_length = 90;
pcb_width = 60;
pcb_origin = [0, 0];
pcb_hole_spacing = [78, 50];
pcb_screw_d = 2.8;       // Pilot/clearance hole; tune for the chosen screw.
pcb_boss_d = 7.0;
pcb_boss_height = 8.0;
tray_thickness = 2.4;
electronics_tower_spacing = [98, 68];
electronics_sled_z = 31;

// Common NEO-6M breakout/patch-antenna sled.
gps_board = [38, 28];
gps_origin = [70, 0];
gps_tower_spacing = [31, 21];
gps_tower_height = 21;
gps_screw_d = 2.6;       // M2.5 clearance.

// Optical ports: TSL2584 sea, TCS3448 colour, TSL2584 land.
optical_port_d = 11.5;
optical_window_d = 17.0; // Recess for a glued clear acrylic/polycarbonate disc.
optical_window_depth = 1.0;
optical_ports = [[-8, 43], [28, 0], [-8, -43]];
optical_mount_spacing = 22;
optical_screw_d = 2.2;   // M2 clearance.

// SHT45 carrier behind the rain-sheltered head vents.
env_origin = [98, 0];
env_mount_spacing = 22;

// Rear PG7-style cable gland holes: DS18B20, soil probes, power/service.
cable_hole_d = 12.5;
cable_y = [-24, 0, 24];
cable_z = 8;

// Standard photographic tripod interface. This plate belongs in/on the lower
// enclosure so the upper lid is not structural. Use a metal 1/4-20 nut/insert.
tripod_plate = [52, 52, 7];
tripod_screw_clearance_d = 6.8;
tripod_nut_af = 11.3;
tripod_nut_depth = 5.5;
tripod_base_hole_spacing = 36;
tripod_base_screw_d = 4.3;

// ---------- helpers ----------

module rounded_plate(size, radius, height, center = true) {
    linear_extrude(height = height, center = center)
        offset(r = radius)
            square([size[0] - 2 * radius, size[1] - 2 * radius], center = true);
}

module capsule_2d(length, width) {
    hull() {
        translate([-(length - width) / 2, 0]) circle(d = width);
        translate([(length - width) / 2, 0]) circle(d = width);
    }
}

module ellipsoid(size, center = [0, 0, 0]) {
    translate(center) scale([size[0] / 2, size[1] / 2, size[2] / 2]) sphere(d = 2);
}

module rotated_ellipsoid(size, center, angle) {
    translate(center) rotate([0, 0, angle]) scale([size[0] / 2, size[1] / 2, size[2] / 2]) sphere(d = 2);
}

module hex_prism(across_flats, height) {
    cylinder(d = across_flats / cos(30), h = height, $fn = 6);
}

module half_ellipsoid(size) {
    intersection() {
        ellipsoid([size[0], size[1], 2 * size[2]]);
        translate([-size[0], -size[1], 0]) cube([2 * size[0], 2 * size[1], size[2] + 1]);
    }
}

module ellipse_prism(length, width, height, z0 = 0) {
    translate([0, 0, z0]) linear_extrude(height = height)
        scale([length / 2, width / 2]) circle(d = 2);
}

module vertical_slot(length, diameter, height, z0) {
    translate([0, 0, z0]) linear_extrude(height = height)
        capsule_2d(length, diameter);
}

function roof_z(x, y, inset = 0) =
    (shell_height - inset) *
    sqrt(max(0, 1 - pow(x / (shell_length / 2 - inset), 2)
                        - pow(y / (shell_width / 2 - inset), 2)));

// ---------- turtle body and hollow shell ----------

module turtle_outer() {
    union() {
        half_ellipsoid([shell_length, shell_width, shell_height]);

        // Flat mating flange around the shell opening.
        ellipse_prism(shell_length + 2 * flange_width,
                      shell_width + 2 * flange_width,
                      flange_height, -0.5);

        // Head, four swimming flippers, and tail create the turtle silhouette.
        rotated_ellipsoid([48, 38, 26], [130, 0, 8], 0);
        rotated_ellipsoid([62, 27, 10], [60, 84, 3], 24);
        rotated_ellipsoid([62, 27, 10], [60, -84, 3], -24);
        rotated_ellipsoid([49, 22, 9], [-65, 80, 2.5], -28);
        rotated_ellipsoid([49, 22, 9], [-65, -80, 2.5], 28);
        rotated_ellipsoid([29, 14, 9], [-127, 0, 2.5], 0);
    }
}

module shell_void() {
    // Opening extends below the flange so the underside remains accessible.
    intersection() {
        ellipsoid([shell_length - 2 * wall,
                   shell_width - 2 * wall,
                   2 * (shell_height - wall)]);
        translate([-shell_length, -shell_width, -lip_drop - 2])
            cube([2 * shell_length, 2 * shell_width, shell_height + lip_drop + 2]);
    }
}

module sealing_lip() {
    // Tongue that enters a matching groove in the lower enclosure.
    difference() {
        ellipse_prism(shell_length - 2 * (wall + fit_clearance),
                      shell_width - 2 * (wall + fit_clearance),
                      lip_drop, -lip_drop);
        ellipse_prism(shell_length - 2 * (wall + lip_wall + fit_clearance),
                      shell_width - 2 * (wall + lip_wall + fit_clearance),
                      lip_drop + 1, -lip_drop - 0.5);
    }
}

// ---------- integrated mounting structure ----------

module powerbank_cradle() {
    bank_l = powerbank[0] + 2 * powerbank_clearance;
    bank_w = powerbank[1] + 2 * powerbank_clearance;

    // Low load-spreading deck; its ends overlap the shell wall/flange.
    translate([powerbank_origin[0], powerbank_origin[1], tray_thickness / 2 + 1.0])
        rounded_plate([bank_l + 12, bank_w + 12], 5, tray_thickness);

    // Four short ribs transfer battery load into both sides of the carapace.
    for (x = [-55, 25], sy = [-1, 1])
        translate([x, sy * 57, tray_thickness / 2 + 1.0])
            cube([10, 38, tray_thickness], center = true);

    // Side and end rails leave the top open for cooling and removal.
    for (y = [-(bank_w + 5) / 2, (bank_w + 5) / 2])
        translate([powerbank_origin[0], y, 1 + powerbank_rail_height / 2])
            cube([bank_l - 18, 3, powerbank_rail_height], center = true);
    for (x = [powerbank_origin[0] - (bank_l + 5) / 2,
              powerbank_origin[0] + (bank_l + 5) / 2])
        translate([x, 0, 1 + powerbank_rail_height / 2])
            cube([3, bank_w - 18, powerbank_rail_height], center = true);
}

module electronics_towers() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([pcb_origin[0] + sx * electronics_tower_spacing[0] / 2,
                   pcb_origin[1] + sy * electronics_tower_spacing[1] / 2,
                   1.0])
            cylinder(d = 8, h = electronics_sled_z - 1.0);
}

module electronics_sled() {
    difference() {
        union() {
            rounded_plate([pcb_length + 14, pcb_width + 12], 5, tray_thickness);

            for (sx = [-1, 1], sy = [-1, 1])
                translate([sx * pcb_hole_spacing[0] / 2,
                           sy * pcb_hole_spacing[1] / 2,
                           tray_thickness / 2])
                    cylinder(d = pcb_boss_d, h = pcb_boss_height);

            // Tie-down tabs support boards without mounting holes.
            for (y = [-pcb_width / 2 - 2, pcb_width / 2 + 2])
                for (x = [-24, 24])
                    translate([x, y, 1.5]) cube([12, 5, 3], center = true);
        }

        for (sx = [-1, 1], sy = [-1, 1]) {
            // Sled-to-tower holes.
            translate([sx * electronics_tower_spacing[0] / 2,
                       sy * electronics_tower_spacing[1] / 2, -4])
                cylinder(d = pcb_screw_d, h = 12);
            // PCB boss holes.
            translate([sx * pcb_hole_spacing[0] / 2,
                       sy * pcb_hole_spacing[1] / 2, 0])
                cylinder(d = pcb_screw_d, h = pcb_boss_height + 5);
        }

        for (y = [-pcb_width / 2 - 2, pcb_width / 2 + 2])
            for (x = [-24, 24])
                translate([x, y, -4]) vertical_slot(8, 2.4, 12, 0);
    }
}

module gps_towers() {
    // This pad overlaps the power-bank deck and the SHT45 shelf, keeping all
    // internal structures connected to the shell rather than floating.
    translate([gps_origin[0], gps_origin[1], tray_thickness / 2 + 1.0])
        rounded_plate([48, 36], 4, tray_thickness);
    for (sx = [-1, 1], sy = [-1, 1]) {
        translate([gps_origin[0] + sx * gps_tower_spacing[0] / 2,
                   gps_origin[1] + sy * gps_tower_spacing[1] / 2,
                   1.0])
            cylinder(d = 7, h = gps_tower_height);
    }
}

module env_shelf() {
    translate([env_origin[0], env_origin[1], tray_thickness / 2 + 1.0])
        rounded_plate([30, 30], 4, tray_thickness);
    for (dx = [-env_mount_spacing / 2, env_mount_spacing / 2])
        translate([env_origin[0] + dx, env_origin[1], 1 + tray_thickness])
            cylinder(d = 6, h = 5);
}

module optical_mount_bosses() {
    for (port = optical_ports) {
        mount_z = roof_z(port[0], port[1], wall) - 7;
        for (dx = [-optical_mount_spacing / 2, optical_mount_spacing / 2])
            translate([port[0] + dx, port[1], mount_z])
                cylinder(d = 6, h = 8);
    }
}

module window_rims() {
    for (port = optical_ports) {
        z = roof_z(port[0], port[1], 0);
        translate([port[0], port[1], z - 0.8])
            difference() {
                cylinder(d = optical_window_d + 4, h = 2.4);
                translate([0, 0, -0.5]) cylinder(d = optical_window_d, h = 3.5);
            }
    }
}

// ---------- openings ----------

module optical_cutouts() {
    for (port = optical_ports) {
        z = roof_z(port[0], port[1], 0);
        translate([port[0], port[1], z - 10])
            cylinder(d = optical_port_d, h = 22);
        translate([port[0], port[1], z - optical_window_depth])
            cylinder(d = optical_window_d, h = optical_window_depth + 3);
    }
}

module optical_mount_holes() {
    for (port = optical_ports) {
        mount_z = roof_z(port[0], port[1], wall) - 8;
        for (dx = [-optical_mount_spacing / 2, optical_mount_spacing / 2])
            translate([port[0] + dx, port[1], mount_z])
                cylinder(d = optical_screw_d, h = 12);
    }
}

module head_vents() {
    // Rain-sheltered "nostrils" on the lower front of the head for the SHT45.
    for (y = [-7, 0, 7], z = [5.5, 10])
        translate([113, y, z]) rotate([0, 90, 0]) cylinder(d = 3.2, h = 42);
}

module cable_cutouts() {
    for (y = cable_y)
        translate([-150, y, cable_z]) rotate([0, 90, 0])
            cylinder(d = cable_hole_d, h = 75);
}

module lid_fastener_holes() {
    for (p = lid_fasteners) {
        translate([p[0], p[1], -lip_drop - 1])
            cylinder(d = lid_screw_d, h = lip_drop + flange_height + 5);
        translate([p[0], p[1], flange_height - 0.6])
            cylinder(d = lid_counterbore_d, h = 4);
    }
}

module mounting_holes() {
    for (sx = [-1, 1], sy = [-1, 1]) {
        translate([pcb_origin[0] + sx * electronics_tower_spacing[0] / 2,
                   pcb_origin[1] + sy * electronics_tower_spacing[1] / 2,
                   electronics_sled_z - 5])
            cylinder(d = pcb_screw_d, h = 9);

        translate([gps_origin[0] + sx * gps_tower_spacing[0] / 2,
                   gps_origin[1] + sy * gps_tower_spacing[1] / 2,
                   gps_tower_height - 3])
            cylinder(d = gps_screw_d, h = 8);
    }

    for (dx = [-env_mount_spacing / 2, env_mount_spacing / 2])
        translate([env_origin[0] + dx, env_origin[1], 1 + tray_thickness])
            cylinder(d = optical_screw_d, h = 8);

    // Two wide hook-and-loop straps hold the power bank in its cradle.
    for (x = [powerbank_origin[0] - powerbank[0] / 4,
              powerbank_origin[0] + powerbank[0] / 4])
        for (y = [-(powerbank[1] + 4) / 2, (powerbank[1] + 4) / 2])
            translate([x, y, -2]) vertical_slot(powerbank_strap_width, 3.2, 10, 0);
}

module lid() {
    difference() {
        union() {
            difference() {
                turtle_outer();
                shell_void();
            }
            sealing_lip();
            powerbank_cradle();
            electronics_towers();
            gps_towers();
            env_shelf();
            optical_mount_bosses();
            window_rims();
        }
        optical_cutouts();
        optical_mount_holes();
        head_vents();
        cable_cutouts();
        lid_fastener_holes();
        mounting_holes();
    }
}

// ---------- separately printed inserts ----------

module gps_sled() {
    difference() {
        union() {
            rounded_plate([gps_board[0] + 8, gps_board[1] + 8], 4, 2.4);
            // Low perimeter stops keep the ceramic patch/module from sliding.
            for (y = [-(gps_board[1] + 4) / 2, (gps_board[1] + 4) / 2])
                translate([0, y, 2.2]) cube([gps_board[0] + 6, 2, 3], center = true);
        }
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * gps_tower_spacing[0] / 2,
                       sy * gps_tower_spacing[1] / 2, -2])
                cylinder(d = gps_screw_d, h = 6);
        // Two crossed zip-tie slots support modules with no screw holes.
        for (x = [-11, 11]) translate([x, 0, -2]) vertical_slot(16, 2.6, 6, 0);
    }
}

module optical_cartridge() {
    difference() {
        rounded_plate([30, 22], 3, 2.4);
        cylinder(d = optical_port_d, h = 7, center = true);
        for (x = [-optical_mount_spacing / 2, optical_mount_spacing / 2])
            translate([x, 0, -3]) cylinder(d = optical_screw_d, h = 8);
        for (y = [-7, 7]) translate([0, y, -2]) vertical_slot(8, 2.2, 6, 0);
    }
}

module env_cartridge() {
    difference() {
        rounded_plate([30, 25], 3, 2.4);
        for (y = [-7, 0, 7])
            translate([0, y, -2]) vertical_slot(14, 2.5, 6, 0);
        for (x = [-env_mount_spacing / 2, env_mount_spacing / 2])
            translate([x, 0, -3]) cylinder(d = optical_screw_d, h = 8);
    }
}

module tripod_adapter() {
    difference() {
        union() {
            rounded_plate([tripod_plate[0], tripod_plate[1]], 6,
                          tripod_plate[2], center = false);
            // Anti-rotation key fits a matching recess in the lower enclosure.
            translate([0, 0, tripod_plate[2]])
                rounded_plate([30, 30], 4, 2.0, center = false);
        }
        // Tripod screw passes upward into a captive metal 1/4-20 nut.
        translate([0, 0, -1])
            cylinder(d = tripod_screw_clearance_d, h = tripod_plate[2] + 5);
        translate([0, 0, tripod_plate[2] - tripod_nut_depth])
            hex_prism(tripod_nut_af, tripod_nut_depth + 3);
        // Four M4 holes transfer bending load into the lower enclosure.
        for (x = [-tripod_base_hole_spacing / 2, tripod_base_hole_spacing / 2],
             y = [-tripod_base_hole_spacing / 2, tripod_base_hole_spacing / 2])
            translate([x, y, -1])
                cylinder(d = tripod_base_screw_d, h = tripod_plate[2] + 5);
    }
}

// ---------- output ----------

if (part == "lid") {
    lid();
} else if (part == "gps_sled") {
    gps_sled();
} else if (part == "electronics_sled") {
    electronics_sled();
} else if (part == "optical_cartridge") {
    optical_cartridge();
} else if (part == "env_cartridge") {
    env_cartridge();
} else if (part == "tripod_adapter") {
    tripod_adapter();
} else {
    color("seagreen") lid();
    color("slategray")
        translate([pcb_origin[0], pcb_origin[1], electronics_sled_z])
            electronics_sled();
    color("gold") translate([gps_origin[0], gps_origin[1], gps_tower_height + 1.2]) gps_sled();
    color("orange")
        translate([env_origin[0], env_origin[1], 1 + tray_thickness + 6.2])
            env_cartridge();
    for (i = [0 : 2])
        color("deepskyblue")
            translate([optical_ports[i][0], optical_ports[i][1],
                       roof_z(optical_ports[i][0], optical_ports[i][1], wall) - 7])
                optical_cartridge();
}

