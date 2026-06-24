# SALVAGE.md — Where to find these parts without buying new

**Purpose:** this project is designed around parts common enough to pull
from dead electronics rather than requiring a parts order. This doc is
the lookup list — what device, what to look for, how confident the claim
is. Meant to be reused/forked for other projects in this ecosystem, not
just this one.

**Honesty note:** chip selection on consumer boards changes across
hardware revisions without the product name changing. Treat "Tier 1" as
verified-common, "Tier 2" as plausible-but-revision-dependent, and always
confirm the actual part number printed on the chip before relying on it.

---

## ATmega328P (or close AVR relatives — see note below)

### Tier 1 — confirmed common, look here first

- **Arduino Uno / Nano / Pro Mini and the enormous clone market** —
  this is the chip's original home and by far the highest-volume source.
  The Arduino Uno R3 is built around the ATmega328P, running at 16MHz with 32KB flash, 2KB SRAM — exactly the chip this firmware targets and was compiled against. Clone boards (the $2-4 ones from import sellers) almost always carry the genuine chip even when everything else is a copy, because the silicon itself isn't worth faking.
- **DIY/hobbyist breakout and programmer boards** — ATmega328P-PU in DIP-28 format is widely used as a standalone chip in hobbyist enclosures, programmer rigs, and breakout boards, often sold loose or in cheap project kits.
- **Small Arduino-compatible add-on/expansion boards** — compact 3D-printer-adjacent expansion boards advertise direct ATmega328P compatibility, and the broader market of "Arduino-compatible" stepper/sensor breakout boards is built on the same chip.

### Tier 2 — plausible, verify the silkscreen before counting on it

- **3D printer controller boards** — be careful here. RAMPS-style boards are frequently paired with an **Arduino Mega 2560 (ATmega2560)**, not a 328P — RAMPS 1.4 is explicitly built to sit on top of an Arduino Mega board, which uses a different (larger, pin-incompatible) chip in the same family. Some all-in-one printer boards (Melzi-style) use an **ATmega1284P** instead — a related but distinct chip with more I/O and flash, not a drop-in match for this firmware without recompiling. If you crack open a 3D printer board, **read the chip markings** — 328P, 2560, and 1284P are all AVR family and conceptually similar to program, but not interchangeable without adjusting fuse settings and pin maps.
- **Older Arduino-based robotics kits and educational electronics** — likely 328P, but check the silkscreen; some budget kits use clones with different/smaller AVR parts to cut cost.

### What to physically look for

The chip itself, if socketed (DIP package), is a black rectangular
28-pin chip stamped "ATMEGA328P" (sometimes "328P-PU" or "328P-AU" for
the surface-mount variant). If it's soldered direct to a board (SMD,
TQFP-32 package), it's a small square chip, same markings, harder to
desolder without proper tools but not impossible with a hot air station
or even a careful iron-and-patience approach.

---

## LM393 (dual comparator)

### Tier 1 — confirmed common

- General-purpose analog sensing circuits across an enormous range of
  consumer electronics: light/sound-activated switches, simple battery
  level indicators, zero-crossing detectors, basic threshold alarms.
  The LM393 (and its single-comparator sibling LM393N, and close
  relatives LM339/LM2903) has been a default "I need a cheap comparator"
  part since the 1970s and shows up across decades of product designs.
- Cheap sound-activated relay modules and "sound sensor" breakout boards
  sold for Arduino hobbyist use almost universally use an LM393 as the
  threshold comparator stage — these are sold standalone for under $1
  and are also easy to identify and pull from broken units.

### What to physically look for

Small 8-pin DIP or SOIC chip, marked "LM393" (sometimes with a
manufacturer prefix like "LM393N" or "NJM393"). If you see an 8-pin chip
near anything doing analog threshold detection — a sound sensor, a light
sensor module, an old battery tester — it's worth checking.

---

## General salvage strategy (applies beyond this project)

1. **Read chip markings before assuming compatibility.** Family
   resemblance (same manufacturer, similar package) is not the same as
   pin-and-firmware compatibility.
2. **Dead 3D printer control boards, old robotics kits, and "smart"
   consumer electronics from the 2012-2020 era** are disproportionately
   likely to carry AVR-family chips and basic analog comparators — this
   was the default cheap-and-good-enough toolkit for a decade of
   consumer electronics design.
3. **Local sourcing (swap meets, secondhand/marketplace listings, repair
   cafes, maker spaces)** is real and works, but is inherently
   local/time-sensitive — this doc intentionally doesn't try to track
   that, since it goes stale fast. If your area has a known source (a
   downtown surplus shop, a regular swap meet, a community recycling
   event), **add it as a GitHub issue using the template below** rather
   than editing this file directly — that keeps location-specific,
   time-sensitive info separate from the durable reference list.

### Issue template for local sourcing tips

```
**Location:** (city/region)
**Where:** (shop name, event, online marketplace)
**What's findable there:** (parts, approximate frequency/reliability)
**Last confirmed:** (date you actually found something there)
```

---

## Contributing to this list

Found this chip somewhere not listed here? Pulled it from something
unexpected? Open a PR. The more complete this list is, the less anyone
building this needs to buy new.
