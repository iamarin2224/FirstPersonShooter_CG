# CG Lab Project — First-Person Shooter Training Range

> **Phase 1** · Qt 6 · C++17 · 2D Rasterized Arcade Shooter

A pixel-art, retro-synthwave **2D target-shooting game** built entirely with Qt's `QWidget` / `QPainter` rasterization API — no OpenGL, no QML.  
All visual elements (gradients, borders, icons, scanlines) are drawn pixel-by-pixel in the same spirit as the CG algorithm demonstrations in `Arin_CG`.

---

## Table of Contents

1. [Project Overview](#1-project-overview)  
2. [Repository Layout](#2-repository-layout)  
3. [Architecture](#3-architecture)  
4. [Constants & Configuration](#4-constants--configuration)  
5. [Rendering Pipeline](#5-rendering-pipeline)  
6. [Target Lifecycle & Async Mechanics](#6-target-lifecycle--async-mechanics)  
7. [Data Structures](#7-data-structures)  
8. [Building & Running](#8-building--running)  
9. [Controls](#9-controls)  
10. [Phase 2 Roadmap](#10-phase-2-roadmap)

---

## 1. Project Overview

| Aspect | Detail |
|--------|--------|
| Framework | Qt 6 (`QWidget`, `QPainter`) |
| Language | C++17 |
| Render rate | 60 FPS via master `QTimer` |
| Active targets | 2 concurrent, independent lifecycles |
| Target lifespan | 2 000 ms each, **staggered by 1 000 ms** |
| Visual style | Synthwave / cyberpunk pixel-art |

The game maintains **exactly two live targets** at all times on a virtual grid.  
Each target has its own independent 2-second countdown.  They are staggered at launch so they never expire simultaneously.  
Left-clicking a target earns a point and immediately relocates it to a new non-overlapping random cell.

---

## 2. Repository Layout

```
CG_Lab_Project/
├── FirstPersonShooter.pro   # qmake project file
├── main.cpp                 # QApplication entry-point
├── gamewidget.h             # GameWidget class + TargetState struct declaration
├── gamewidget.cpp           # Full rendering & game-logic implementation
├── build/                   # Out-of-source build artefacts (generated)
│   └── FirstPersonShooter.app/
└── README.md                # This file
```

---

## 3. Architecture

```
QApplication
    └── GameWidget  (QWidget)
            │
            ├── m_renderTimer  (QTimer, 16 ms / ~60 Hz)
            │       └── onRenderTick()
            │               ├── updates ageFraction & pulsePhase for each target
            │               └── calls update() → paintEvent()
            │
            ├── m_targets[0].timer  (QTimer, 2 000 ms, single-shot, repeating)
            │       └── onTarget0Expired() → relocateTarget(0) → restart timer
            │
            └── m_targets[1].timer  (QTimer, 2 000 ms, single-shot, repeating)
                    └── onTarget1Expired() → relocateTarget(1) → restart timer
```

### Key design decisions

* **Two independent timers, not one shared timer.**  
  Each `TargetState` owns a `QTimer*`.  The render loop reads `timer->remainingTime()` to compute `ageFraction` without any shared counters.

* **Out-of-source build.**  
  The `build/` directory is separated so the source tree stays clean.

* **Rasterized drawing only.**  
  Every gradient, border and icon is rendered with `p.drawLine()` / `p.fillRect()` loops, matching the style of the reference `DrawLine` project.

---

## 4. Constants & Configuration

All tuneable values live at the top of `gamewidget.h`:

| Constant | Default | Meaning |
|----------|---------|---------|
| `CELL_W` | `60` px | Grid cell width |
| `CELL_H` | `60` px | Grid cell height |
| `TARGET_COLS` | `2` | Target width in grid cells |
| `TARGET_ROWS` | `2` | Target height in grid cells |
| `TARGET_LIFESPAN_MS` | `2000` ms | Time before a target relocates |
| `RENDER_INTERVAL_MS` | `16` ms | Master paint tick (~60 FPS) |

---

## 5. Rendering Pipeline

`paintEvent()` calls five sub-routines in order:

```
paintEvent()
 ├── drawBackground()   — sky gradient (scanline-by-scanline) + vanishing-point floor grid
 ├── drawGrid()         — cell-aligned blue-violet grid lines (major lines every 4 cells)
 ├── drawScanlines()    — CRT-style scanline darkening (every other row)
 ├── drawTarget() × 2  — one call per active target (see below)
 └── drawHUD()          — score, miss count, per-target countdown bars
```

### `drawTarget()` detail

Each target is rendered using six raster passes:

1. **Body gradient** — vertical scanlines, colour derived from the target's accent (`TARGET_ACCENT[id]`)
2. **Inner circle** — midpoint-algorithm-style filled circle in the accent's alpha variant
3. **Pixel-art icon** — a 16 × 16 bitmap (`TARGET_ICON[]`) scaled up by `iconScale` pixels per bit
4. **Pulsing neon border** — 3-pixel-wide inset frame, alpha driven by `sin(pulsePhase)`
5. **Corner tick marks** — white 6-pixel L-shaped corners for retro HUD feel
6. **Age countdown bar** — a 4-pixel-tall progress bar at the bottom of the target (green → empties left to right as the timer counts down)

---

## 6. Target Lifecycle & Async Mechanics

```
t = 0 ms    → relocateTarget(0)  →  m_targets[0].timer.start(2000)
t = 1000 ms → relocateTarget(1)  →  m_targets[1].timer.start(2000)

t = 2000 ms → onTarget0Expired() → relocateTarget(0) → timer.start(2000)
t = 3000 ms → onTarget1Expired() → relocateTarget(1) → timer.start(2000)
              … and so on indefinitely …
```

**`relocateTarget(idx)`** algorithm:

1. Read the *other* target's current `(gridCol, gridRow)`.
2. Sample a random `(col, row)` with uniform distribution over the valid grid.
3. Reject and resample if the candidate overlaps the other target (`cellsOverlap()`).
4. Retry up to 200 times (avoids infinite loops on very small grids).
5. Write new `gridCol`, `gridRow`, `rect`, reset `ageFraction = 0`.

**Click-to-shoot** (`mousePressEvent`):

If the click point falls inside a target's `QRect`, score is incremented, `relocateTarget()` is called immediately and the target's timer is restarted — effectively giving that target a fresh 2-second window in its new position.

---

## 7. Data Structures

### `TargetState` (in `gamewidget.h`)

```cpp
struct TargetState {
    int    gridCol;       // top-left grid column index
    int    gridRow;       // top-left grid row index
    QRect  rect;          // screen-space bounding rect (pixels)
    QTimer* timer;        // independent 2-second lifecycle timer
    qreal  ageFraction;   // [0, 1] — 0 = just spawned, 1 = about to relocate
    qreal  pulsePhase;    // [0, 2π) — drives border shimmer animation
    int    id;            // 0 or 1 — selects colour accent
};
```

### `GameWidget` members

| Member | Type | Purpose |
|--------|------|---------|
| `m_targets[2]` | `TargetState` | The two live targets |
| `m_renderTimer` | `QTimer` | 60 Hz master repaint driver |
| `m_wallClock` | `QElapsedTimer` | Wall time for smooth animation |
| `m_gridCols/Rows` | `int` | Grid dimensions (recalculated on resize) |
| `m_score` | `int` | Hits this session |
| `m_misses` | `int` | Targets that expired without being shot |
| `m_rng` | `std::mt19937` | Seeded Mersenne-Twister for spawn positions |

---

## 8. Building & Running

### Prerequisites

| Requirement | Version tested |
|-------------|----------------|
| Qt | 6.11.1 (macOS arm64) |
| Compiler | Apple Clang 21 (Xcode) |
| CMake / qmake | qmake (bundled with Qt) |
| C++ standard | C++17 |

### Build steps

```bash
# 1. Clone / open the project folder
cd /path/to/CG_Lab_Project

# 2. Generate Makefile (out-of-source)
mkdir -p build && cd build
/Users/arindas/Qt/6.11.1/macos/bin/qmake ../FirstPersonShooter.pro

# 3. Compile
make -j$(sysctl -n hw.logicalcpu)

# 4. Run
open FirstPersonShooter.app
# or
./FirstPersonShooter.app/Contents/MacOS/FirstPersonShooter
```

> **Qt Creator shortcut**: Open `FirstPersonShooter.pro` in Qt Creator, press **⌘R** to build and run.

---

## 9. Controls

| Input | Action |
|-------|--------|
| **Left click** on target | Hit — score +1, target instantly relocates |
| **Left click** on background | Miss (no penalty, counter not incremented) |
| Window resize | Grid recomputes; target rects update automatically |

---

## 10. Phase 2 Roadmap

Planned additions for the next phase:

- [ ] **Crosshair overlay** — rendered pixel-by-pixel, tracks mouse position
- [ ] **Miss counter** — flash visual feedback on expired miss
- [ ] **Difficulty scaling** — shorter lifespan as score increases
- [ ] **Sound effects** — `QSoundEffect` hit / miss audio
- [ ] **Animated target entrance** — fade-in over first 200 ms using `ageFraction`
- [ ] **High-score persistence** — `QSettings`-backed leaderboard
- [ ] **Moving targets** — targets drift slowly across their cell each frame
- [ ] **CMake migration** — replace `.pro` with `CMakeLists.txt` for Qt 6 best practice