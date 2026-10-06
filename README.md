# CG Lab Project — First-Person Shooter Training Range

> **Phase 2 · Pixel Reflex** · Qt 6 · C++17 · 2D Rasterized Arcade Shooter

A grid-based reflex and restraint game built with Qt's `QWidget` / `QPainter` API. This version evolves the original synthwave target shooter into a cream-colored coordinate grid inspired by the CG drawing exercises. Each target fills exactly one grid cell. The player must recognize the color before clicking: chase rewards, resist surprise penalties.

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Repository Layout](#2-repository-layout)
3. [Architecture](#3-architecture)
4. [Constants & Configuration](#4-constants--configuration)
5. [Rendering Pipeline](#5-rendering-pipeline)
6. [Target Lifecycle & Timing](#6-target-lifecycle--timing)
7. [Data Structures](#7-data-structures)
8. [Building & Running](#8-building--running)
9. [Controls](#9-controls)
10. [Future Roadmap](#10-future-roadmap)

## 1. Project Overview

| Aspect | Behavior |
|--------|----------|
| Framework | Qt 6 Widgets; no OpenGL or QML |
| Language | C++17 |
| Rendering | Precise 16 ms timer, approximately 60 FPS |
| Board | Cream cells, thin beige grid, dark coordinate axes |
| Target size | One 32 × 32 px cell; axes excluded |
| Concurrent targets | Up to five, independently expiring |
| Green | +10 points; 12% spawn probability |
| Blue | +1 point; 64% spawn probability |
| Red | −5 points; 24% spawn probability; shorter lifetime |
| Score panel | Total, per-color arithmetic, level, recent clicks, missed rewards |

Probabilities apply independently to each spawn, rather than guaranteeing a fixed sequence. Red targets appear without a warning and should be left alone. Clicking empty space has no penalty. Expired green or blue cells count as missed rewards without subtracting points; expired red cells have no penalty. Scores can become negative.

## 2. Repository Layout

```text
FirstPersonShooter_CG/
├── FirstPersonShooter.pro
├── main.cpp              # QApplication entry point
├── gamewidget.h          # Game state and widget declaration
├── gamewidget.cpp        # Rendering, timing, input, scoring
├── build/                # Generated, ignored build artifacts
│   └── FirstPersonShooter.app/
└── README.md
```

Changes for this version are on `feature/pixel-reflex`.

## 3. Architecture

```text
QApplication
└── GameWidget
    ├── QElapsedTimer → monotonic frame delta
    ├── QTimer (16 ms) → tick → expiry / scheduled spawn → repaint
    ├── TargetState vector → independent birth times and lifetimes
    ├── ScoreEvent history → recent clicks and floating feedback
    └── QPushButton controls → pause / restart
```

One master timer updates the simulation. Spawn scheduling and expiry use elapsed game time instead of separate per-target timers, avoiding competing callbacks. Pausing freezes game time and animations. A frame advances by at most 100 ms after an application stall, preventing a burst of spawns or instant disappearance when execution resumes.

## 4. Constants & Configuration

Values currently live in `gamewidget.h` and `gamewidget.cpp`.

| Setting | Value |
|---------|-------|
| Cell size | 32 px |
| Initial / minimum window | 1280 × 820 / 960 × 740 |
| Color probabilities | Green 12%, blue 64%, red 24% |
| Points | Green +10, blue +1, red −5 |
| Level | `min(10, peakScore / 20 + 1)` |
| Spawn interval | `max(260, 1000 - (level - 1) * 85)` ms |
| Reward lifetime | `max(750, 2300 - (level - 1) * 155)` ms |
| Red lifetime | 75% of reward lifetime, minimum 600 ms |
| Fade in / out | 90 / 140 ms |
| Hit feedback | 650 ms |

Difficulty follows the highest score reached. Losing points does not slow the game down. At level 1, a new cell appears roughly once per second; at level 10, approximately 4.26 cells appear per second. Each target keeps the lifetime assigned at spawn. The initial target appears immediately; the next is scheduled after 650 ms.

## 5. Rendering Pipeline

`paintEvent()` draws the following layers:

1. Warm page background and cream playing board.
2. Dark central axis cells, fine grid lines, border, and `+x` / `+y` labels.
3. Colored single-cell targets with gentle opacity transitions and a thin remaining-time strip.
4. Floating signed score feedback after successful clicks.
5. Right-side score card with live calculation and recent click results.
6. A translucent pause overlay when paused.

Targets stay aligned to their grid cells. No bullseye multiplier or crosshair is used in this version; every point within a colored cell has the same value.

## 6. Target Lifecycle & Timing

On each active frame, expired targets are removed and at most one scheduled target is spawned. A random non-axis, unoccupied cell is chosen with bounded retries. Its color is sampled independently, then its birth time and lifetime are recorded. Clicking a target applies its signed score once and removes it immediately. The next target follows the spawn schedule instead of teleporting on every hit.

Resizing recomputes the available board dimensions. Targets outside the new board are removed; all remaining cells retain their grid positions. Restart clears score, timing, history, and difficulty.

## 7. Data Structures

| Structure | Fields / purpose |
|-----------|------------------|
| `TargetState` | Column, row, color kind, birth time, lifetime |
| `ScoreEvent` | Color kind, resulting total, click time, feedback position |
| `m_targets` | Active cells |
| `m_hits` | Hit counts for green, blue, red |
| `m_history` | Last six clicks; newest three shown in the sidebar |
| `m_score`, `m_peak` | Current total and highest score for difficulty |
| `m_now`, `m_nextSpawn` | Simulation time and next scheduled spawn |
| `m_rng` | Random-device-seeded Mersenne Twister |

## 8. Building & Running

Prerequisites: Qt 6 with Widgets and qmake, a C++17 compiler, and make. Built locally with Qt 6.11.1 and Apple Clang 21 on macOS arm64.

```bash
cd FirstPersonShooter_CG
mkdir -p build
cd build
# Use your Qt 6 installation's qmake:
/Users/rittickdutta/Qt/6.11.1/macos/bin/qmake ../FirstPersonShooter.pro
make -j8
open FirstPersonShooter.app
```

If Qt 6 is on your PATH, use `qmake ../FirstPersonShooter.pro`. On Linux, run `./FirstPersonShooter` after building. In Qt Creator, open `FirstPersonShooter.pro` and choose Run.

## 9. Controls

| Input | Action |
|-------|--------|
| Left-click green cell | +10 |
| Left-click blue cell | +1 |
| Left-click red cell | −5 |
| Left-click empty cell | No effect |
| Space / Pause button | Pause or resume |
| R / Restart button | Start a fresh game |
| Resize window | Recompute board dimensions |

The objective is to build points while resisting the impulse to click every new color. Rare green rewards invite quick reactions; sudden red cells reward restraint.

## 10. Future Roadmap

- [ ] Persistent high scores with `QSettings`
- [ ] Optional sound and accessibility settings
- [ ] Timed rounds and end-of-round statistics
- [ ] User-adjustable difficulty and grid size
- [ ] CMake build support alongside qmake
