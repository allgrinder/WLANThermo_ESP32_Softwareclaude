#!/usr/bin/env python3
"""
WLANThermo Pitmaster AddOn - El Fuego Portland - MG995-Variante

Erzeugt aus dem Original (SG90) eine Variante fuer einen MG995 Standard-Servo:
  - Gehaeuse:   Fuss (Kleeblatt-Aufnahme, Magnete, Gummifuesse, Schriftzug) bleibt
                1:1 vom Original, darueber eine neue Haube mit Servo-Montageplatte.
  - Deckel:     Haube ueber dem herausstehenden Servo-Ruecken, mit Tiny-XLR-Loch.
  - Kupplung:   Unterteil (D-Aufnahme fuer die Ventilachse) 1:1 vom Original,
                neuer Kopf mit Klauen fuer das MG995-Kreuzhorn.

Koordinaten intern: Z = Hoehe ab Auflageflaeche am Grill (z=0), Servowelle auf
der Z-Achse. Ausgabe der STLs in Druckorientierung.

Benoetigt: pip install manifold3d trimesh numpy
Aufruf:    python3 generate.py            (schreibt STLs nach ./stl)
"""
import math
import os

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, Mesh

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG_HOUSING = os.path.join(HERE, "original", "Gehaeuse_SG90.stl")
ORIG_COUPLER = os.path.join(HERE, "original", "VerbindungServoPortland_SG90.stl")
OUT_DIR = os.path.join(HERE, "stl")

# ---------------------------------------------------------------------------
# Parameter - MG995 (BITTE NACHMESSEN, Werte sind typische Normservo-Masse)
# ---------------------------------------------------------------------------
SERVO_L = 40.8            # Gehaeuselaenge ohne Laschen
SERVO_W = 20.0            # Gehaeusebreite
SERVO_SHAFT_FROM_END = 10.2   # Mitte Abtriebswelle bis naechste Stirnseite
SERVO_TAB_SPAN = 54.5     # Laenge ueber beide Laschen
SERVO_HOLE_PITCH = 49.5   # Lochabstand in Laengsrichtung (Mitte-Mitte)
SERVO_HOLE_PITCH_W = 10.0 # Lochabstand quer (2 Loecher pro Lasche)
TAB_TO_HORN = 15.5        # Laschenflaeche (wellenseitig) bis Unterkante montiertes Horn
TAB_TO_BOTTOM = 29.5      # Laschenflaeche (wellenseitig) bis Servoboden
SERVO_CLEARANCE = 0.4     # Luft rund um den Servokoerper

# Kreuzhorn (MG995 Zubehoer) - NACHMESSEN
HORN_ARM_W = 6.0          # Armbreite nahe der Nabe
HORN_ARM_CLEAR = 0.4      # Spiel in der Klaue (gesamt)
HORN_HUB_D = 13.0         # Nabendurchmesser (servoseitig)
HORN_SWEEP_R = 21.0       # groesster Hornradius (Arm-Spitze) fuer Freigang

# Lage
SERVO_DIR_DEG = 90.0      # Richtung, in die der Servokoerper von der Welle aus zeigt
                          # (90 = wie der SG90 im Original, 0 = Richtung Tiny-XLR-Loch)
HORN_Z = 29.0             # Hoehe Horn-Unterkante ueber Grillfront
COUPLER_Z0 = 0.5          # Hoehe Kupplungsunterkante ueber Grillfront (wie Original)
HORN_GAP = 1.0            # Luft zwischen Horn und Klauenboden

# Gehaeuse
BASE_CUT_Z = 22.0         # bis hier wird das Original-Gehaeuse uebernommen
BASE_R = 31.51            # Aussenradius des Originals auf BASE_CUT_Z
PLATE_BAND = 10.0         # senkrechter Rand unter der Montageplatte (Deckelsitz)
SIDE_R = 28.0             # Rundung der Haube auf der Wellenseite
TIP_EXTRA = 3.5           # Material hinter der Laschen-Bohrung an der Spitze
TIP_HALF_W = 14.0         # halbe Breite der Haube an der Spitze
SCREW_PILOT_D = 2.7       # Kernloch fuer M3 (selbstschneidend / Blechschraube 2,9)
SCREW_DEPTH = 10.0
SLOT = 1.5                # Langloch +- in Laengsrichtung (Toleranz Lochabstand)

# Deckel
CAP_CLEAR = 0.3
CAP_WALL = 2.4
CAP_TOP = 2.4
CAP_SLEEVE = 7.0          # Ueberlappung Deckel ueber Gehaeuse-Rand
CAP_HEADROOM = 3.0        # Luft ueber dem Servoboden
CAP_SCREW_D = 3.4
CAP_SCREW_Z = 4.0         # unter Plattenoberkante
XLR_D = 6.5               # wie Original
XLR_U = -19.5             # Position Tiny-XLR (Servo-Koordinaten, Welle = 0)

# Kupplung
COUPLER_CUT_Z = 19.5      # bis hier wird die Original-Kupplung uebernommen
COUPLER_HEAD_R = 15.0
COUPLER_FLARE_DEG = 40.0  # Ueberhang der Kopfaufweitung (gedruckt Fuss unten)
CLAW_H = 4.5
CENTER_HOLE_D = 3.5       # Durchgang zur D-Bohrung (Abziehen mit Schraubendreher)
SCREW_HEAD_POCKET_D = 8.0 # Platz fuer Hornschraube
SCREW_HEAD_POCKET_H = 2.5

SEG = 128

# abgeleitete Groessen
PLATE_Z = HORN_Z + TAB_TO_HORN                 # Oberkante Montageplatte
SERVO_C = SERVO_L / 2 - SERVO_SHAFT_FROM_END   # Gehaeusemitte relativ zur Welle
HOLE_U = (SERVO_C - SERVO_HOLE_PITCH / 2, SERVO_C + SERVO_HOLE_PITCH / 2)
WIN_U = (SERVO_C - SERVO_L / 2 - SERVO_CLEARANCE, SERVO_C + SERVO_L / 2 + SERVO_CLEARANCE)
WIN_V = SERVO_W / 2 + SERVO_CLEARANCE
TIP_U = HOLE_U[1] + SLOT + SCREW_PILOT_D / 2 + TIP_EXTRA


# ---------------------------------------------------------------------------
def load_stl(path):
    """Original laden (Y = Hoehe) und nach Z = Hoehe drehen."""
    t = trimesh.load(path)
    v = t.vertices
    v = np.column_stack([v[:, 0], -v[:, 2], v[:, 1]])  # Drehung um X, +90 Grad
    return Manifold(Mesh(vert_properties=v.astype(np.float32),
                         tri_verts=t.faces.astype(np.uint32)))


def save_stl(m, name):
    mesh = m.simplify(0.001).to_mesh()   # entfernt Splitter-Dreiecke (< 1 um)
    mesh.merge()
    t = trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)
    os.makedirs(OUT_DIR, exist_ok=True)
    t.export(os.path.join(OUT_DIR, name))
    print(f"{name}: {t.extents.round(1)} mm, {t.volume / 1000:.1f} cm3, watertight={t.is_watertight}")


def rrect(u0, u1, v, r):
    """Rechteck in Servo-Koordinaten (u laengs, v quer) mit Eckenradius r."""
    return (CrossSection.square([u1 - u0 - 2 * r, 2 * v - 2 * r])
            .translate([u0 + r, -v + r])
            .offset(r, JoinType.Round, circular_segments=48))


def to_world(cs):
    return cs.rotate(SERVO_DIR_DEG)


def prism(cs, z0, z1):
    return Manifold.extrude(cs, z1 - z0).translate([0, 0, z0])


def outline():
    """Grundriss der Haube: Kreis um die Welle + Spitze ueber dem Servo."""
    circle = CrossSection.circle(SIDE_R, SEG)
    tip = rrect(-5.0, TIP_U, TIP_HALF_W, 6.0)
    return to_world(CrossSection.batch_hull([circle, tip]))


def hole_points():
    return [(u, s * SERVO_HOLE_PITCH_W / 2) for u in HOLE_U for s in (-1, 1)]


def slot_cs(u, v, d, slot):
    return to_world(CrossSection.batch_hull([
        CrossSection.circle(d / 2, 32).translate([u - slot, v]),
        CrossSection.circle(d / 2, 32).translate([u + slot, v])]))


def horizontal_hole(u, z, d, length, inward):
    """Bohrung entlang der Servo-Laengsachse, von u aus 'length' nach innen."""
    c = Manifold.cylinder(length, d / 2, circular_segments=32)
    c = c.rotate([0, 90 if inward > 0 else -90, 0]).translate([u, 0, z])
    return c.rotate([0, 0, SERVO_DIR_DEG])


# ---------------------------------------------------------------------------
def housing():
    orig = load_stl(ORIG_HOUSING)
    base = orig.trim_by_plane([0, 0, -1], -BASE_CUT_Z)   # behalte z <= BASE_CUT_Z

    # Innenkontur des Originals auf Schnitthoehe (Kleeblatt), konvex gemacht
    polys = orig.slice(BASE_CUT_Z).to_polygons()
    inner = min(polys, key=lambda p: np.ptp(p[:, 0]))
    inner_hull = CrossSection.hull_points(inner).offset(0.2, JoinType.Round)

    ol = outline()
    eps = 0.01
    # Uebergang Fuss -> Haube, darueber senkrechter Rand als Deckelsitz
    outer = Manifold.batch_hull([
        prism(CrossSection.circle(BASE_R - 0.3, SEG), BASE_CUT_Z - 0.5, BASE_CUT_Z),
        prism(ol, PLATE_Z - PLATE_BAND, PLATE_Z - PLATE_BAND + eps),
    ])
    outer += prism(ol, PLATE_Z - PLATE_BAND, PLATE_Z - 0.8)
    outer += Manifold.batch_hull([                                     # Fase am Druckbett
        prism(ol, PLATE_Z - 1.0, PLATE_Z - 0.8),
        prism(ol.offset(-0.8, JoinType.Round), PLATE_Z - eps, PLATE_Z),
    ])

    window = to_world(rrect(WIN_U[0], WIN_U[1], WIN_V, 1.5))
    void = Manifold.batch_hull([
        prism(inner_hull, BASE_CUT_Z - 1.0, BASE_CUT_Z),
        prism(CrossSection.circle(HORN_SWEEP_R + 1.5, SEG), BASE_CUT_Z, HORN_Z + 4.0),
    ])
    void = void + prism(window, HORN_Z + 3.5, PLATE_Z + 1)
    # 45-Grad-Schraege unter dem Fenster statt flacher Decke (Druck ohne Stuetzen)
    taper = WIN_V - 1.0
    void += Manifold.batch_hull([
        prism(window, HORN_Z + 3.5, HORN_Z + 3.5 + eps),
        prism(window.offset(-taper, JoinType.Round), HORN_Z + 3.5 - taper, HORN_Z + 3.5 - taper + eps),
    ])

    hood = outer - void
    for (u, v) in hole_points():
        hood -= prism(slot_cs(u, v, SCREW_PILOT_D, SLOT), PLATE_Z - SCREW_DEPTH, PLATE_Z + 1)
    # Deckelschrauben (Kernloch) an beiden Enden
    z = PLATE_Z - CAP_SCREW_Z
    hood -= horizontal_hole(-SIDE_R - 1, z, SCREW_PILOT_D, 11, +1)
    hood -= horizontal_hole(TIP_U + 1, z, SCREW_PILOT_D, 9, -1)

    # Ueberlappung mit dem Fuss statt stumpfer Stoss (vermeidet Nahtkanten)
    hood = hood.trim_by_plane([0, 0, 1], BASE_CUT_Z - 0.5)
    return base + hood


def cap():
    ol = outline()
    z0 = PLATE_Z - CAP_SLEEVE
    z_in = PLATE_Z + TAB_TO_BOTTOM + CAP_HEADROOM
    inner = ol.offset(CAP_CLEAR, JoinType.Round)
    outer = inner.offset(CAP_WALL, JoinType.Round)
    c = prism(outer, z0, z_in + CAP_TOP) - prism(inner, z0 - 1, z_in)
    # Tiny-XLR im Deckel oben
    xlr = to_world(CrossSection.circle(XLR_D / 2, 48).translate([XLR_U, 0]))
    c -= prism(xlr, z_in - 1, z_in + CAP_TOP + 1)
    z = PLATE_Z - CAP_SCREW_Z
    span = CAP_CLEAR + CAP_WALL + 2
    c -= horizontal_hole(-SIDE_R - span, z, CAP_SCREW_D, span + 0.5, +1)
    c -= horizontal_hole(TIP_U + span, z, CAP_SCREW_D, span + 0.5, -1)
    return c


def coupler():
    orig = load_stl(ORIG_COUPLER)
    low = orig.trim_by_plane([0, 0, -1], -COUPLER_CUT_Z)

    # Aussenradius des Originals an der Schnittstelle
    pts = np.array([p for poly in orig.slice(COUPLER_CUT_Z).to_polygons() for p in poly])
    r0 = np.linalg.norm(pts, axis=1).max()

    z_floor = HORN_Z - HORN_GAP - COUPLER_Z0          # Klauenboden (lokal)
    z_top = z_floor + CLAW_H
    flare_h = (COUPLER_HEAD_R - r0) / math.tan(math.radians(COUPLER_FLARE_DEG))
    z_flare = COUPLER_CUT_Z + flare_h
    assert z_flare <= z_floor, "Kopf zu kurz fuer die Aufweitung - HORN_Z erhoehen"

    eps = 0.01
    head = Manifold.batch_hull([
        Manifold.cylinder(eps, r0, circular_segments=SEG).translate([0, 0, COUPLER_CUT_Z - eps]),
        Manifold.cylinder(z_floor - z_flare + eps, COUPLER_HEAD_R,
                          circular_segments=SEG).translate([0, 0, z_flare]),
    ])
    claws = Manifold.cylinder(CLAW_H, COUPLER_HEAD_R, circular_segments=SEG).translate([0, 0, z_floor])
    claws -= Manifold.cylinder(CLAW_H + 2, HORN_HUB_D / 2 + 0.5,
                               circular_segments=SEG).translate([0, 0, z_floor - 1])
    w = HORN_ARM_W + HORN_ARM_CLEAR
    for ang in (0, 90):
        claws -= Manifold.cube([2 * COUPLER_HEAD_R + 2, w, CLAW_H + 2], center=True) \
            .translate([0, 0, z_floor + CLAW_H / 2]).rotate([0, 0, ang])

    c = low + head + claws
    c -= Manifold.cylinder(SCREW_HEAD_POCKET_H + 1, SCREW_HEAD_POCKET_D / 2,
                           circular_segments=48).translate([0, 0, z_floor - SCREW_HEAD_POCKET_H])
    c -= Manifold.cylinder(z_top, CENTER_HOLE_D / 2, circular_segments=32).translate([0, 0, 5])
    # Kegelspitze statt flacher Decke ueber der D-Bohrung (druckt ohne Bruecke)
    c -= Manifold.cylinder(4.0 - CENTER_HOLE_D / 2, 4.0, CENTER_HOLE_D / 2,
                           circular_segments=48).translate([0, 0, COUPLER_CUT_Z - 0.01])
    return c, z_top


SERVO_TAB_T = 2.5         # nur fuer Dummy/Pruefung
SERVO_TAB_TO_CASE = 8.0   # Laschenflaeche bis Getriebedeckel (Dummy)


def servo_dummy():
    """Vereinfachter MG995 in Einbaulage (Welle nach unten) + Kreuzhorn."""
    body = to_world(rrect(SERVO_C - SERVO_L / 2, SERVO_C + SERVO_L / 2, SERVO_W / 2, 0.5))
    tabs = to_world(rrect(SERVO_C - SERVO_TAB_SPAN / 2, SERVO_C + SERVO_TAB_SPAN / 2,
                          SERVO_W / 2 - 1, 0.5))
    s = prism(body, PLATE_Z - SERVO_TAB_TO_CASE, PLATE_Z + TAB_TO_BOTTOM)
    s += prism(tabs, PLATE_Z, PLATE_Z + SERVO_TAB_T)
    dome_z = PLATE_Z - SERVO_TAB_TO_CASE
    s += Manifold.cylinder(dome_z - HORN_Z, 6.5, circular_segments=48).translate([0, 0, HORN_Z])
    horn = Manifold.cylinder(5, HORN_HUB_D / 2, circular_segments=48).translate([0, 0, HORN_Z])
    for ang in (0, 90):
        horn += Manifold.cube([2 * HORN_SWEEP_R, HORN_ARM_W, 2.3], center=True) \
            .translate([0, 0, HORN_Z + 1.15]).rotate([0, 0, ang])
    sweep = Manifold.cylinder(2.3, HORN_SWEEP_R, circular_segments=SEG).translate([0, 0, HORN_Z])
    return s, horn, sweep


def check(h, cp, k):
    servo, horn, sweep = servo_dummy()
    kk = k.translate([0, 0, COUPLER_Z0])
    rows = [
        ("Servo  x Gehaeuse", servo ^ h),
        ("Servo  x Deckel", servo ^ cp),
        ("Horn-Drehkreis x Gehaeuse", sweep ^ h),
        ("Horn   x Kupplung", horn ^ kk),
        ("Kupplung x Gehaeuse", kk ^ h),
        ("Deckel x Gehaeuse", cp ^ h),
    ]
    print("Kollisionspruefung (Volumen in mm3, soll 0):")
    for name, m in rows:
        print(f"  {name:28s} {m.volume():8.2f}")
    claw_band = Manifold.cylinder(2.3, COUPLER_HEAD_R, circular_segments=SEG) \
        .translate([0, 0, HORN_Z])
    overlap = (claw_band ^ kk).volume()
    print(f"  Klauen auf Hornhoehe (Eingriff, soll > 0): {overlap:.0f} mm3")
    return servo + horn


def main():
    print(f"Montageplatte auf z={PLATE_Z:.1f} mm, Servo-Mitte u={SERVO_C:.1f}, "
          f"Laschenbohrungen u={HOLE_U[0]:.2f}/{HOLE_U[1]:.2f}")
    h = housing()
    # Druckorientierung: Montageplatte aufs Bett
    save_stl(h.mirror([0, 0, 1]).translate([0, 0, PLATE_Z]), "Gehaeuse_MG995.stl")
    cp = cap()
    top = PLATE_Z + TAB_TO_BOTTOM + CAP_HEADROOM + CAP_TOP
    save_stl(cp.mirror([0, 0, 1]).translate([0, 0, top]), "Deckel_MG995.stl")
    k, ktop = coupler()
    save_stl(k, "Kupplung_MG995.stl")
    print(f"Kupplung: Laenge {ktop:.1f} mm (Original 25.0 mm)")
    dummy = check(h, cp, k)
    # Baugruppe (Einbaulage) zur Kontrolle, Servo als Dummy
    save_stl(h + k.translate([0, 0, COUPLER_Z0]) + dummy, "_Baugruppe_ohne_Deckel.stl")


if __name__ == "__main__":
    main()
