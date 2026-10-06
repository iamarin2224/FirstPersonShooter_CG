// ============================================================
//  gamewidget.cpp — Phase 1: FPS Target Shooter (Qt / C++)
//  CG Lab Project — 2D Rasterized Arcade Shooter
//
//  Rendering philosophy
//  ────────────────────
//  All drawing is done with QPainter operating on raster
//  (pixel-level) coordinates.  We deliberately avoid Qt's
//  high-level shape helpers for the "pixel-art" portions;
//  instead we place individual pixels / small rectangles the
//  same way the reference DrawLine project does.
//
//  Architecture
//  ────────────
//  • m_renderTimer  — single 60 Hz master timer drives repaint()
//                     and updates per-target ageFraction / pulsePhase.
//  • m_targets[0].timer / m_targets[1].timer
//                   — each target owns an *independent* 2-second
//                     single-shot timer.  When it fires the target
//                     is relocated to a non-overlapping random cell.
//  • Target 0 spawns at t = 0 ms, Target 1 at t = 1000 ms so
//    they are always staggered.
// ============================================================

#include "gamewidget.h"

#include <QPainter>
#include <QBrush>
#include <QPen>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QRandomGenerator>
#include <cmath>

// ── Synthwave palette ─────────────────────────────────────────────
static const QColor COL_BG_DARK     { 0x0D, 0x0D, 0x1A };  // near-black indigo
// COL_BG_MID reserved for future layered background passes
static const QColor COL_GRID        { 0x1A, 0x1A, 0x3F, 80 };
static const QColor COL_GRID_BRIGHT { 0x2B, 0x2B, 0x66, 140 };
static const QColor COL_SCANLINE    { 0x00, 0x00, 0x00, 30 };
static const QColor COL_FLOOR      { 0x08, 0x06, 0x18 };

// Per-target accent colours (two distinct neon accents)
static const QColor TARGET_ACCENT[2] {
    { 0xFF, 0x2D, 0xD4 },   // neon magenta  (target 0)
    { 0x00, 0xFF, 0xC8 }    // neon cyan-green (target 1)
};

// COL_HUD_TEXT / COL_CROSSHAIR reserved for Phase 2

// (pixel-art icon data removed — targets now use rasterized concentric rings)

// ================================================================
//  Constructor / Destructor
// ================================================================
GameWidget::GameWidget(QWidget* parent)
    : QWidget(parent)
    , m_rng(std::random_device{}())
{
    setWindowTitle("FPS Training Range — CG Lab Project");
    setMinimumSize(800, 600);
    resize(1100, 720);

    // Mouse tracking for crosshair
    setMouseTracking(true);

    // Background colour (also shown before first paint)
    QPalette pal = palette();
    pal.setColor(QPalette::Window, COL_BG_DARK);
    setPalette(pal);

    // ── Initialise targets ────────────────────────────────────────
    for (int i = 0; i < 2; ++i) {
        m_targets[i].id    = i;
        m_targets[i].timer = new QTimer(this);
        m_targets[i].timer->setSingleShot(true);
        m_targets[i].timer->setInterval(TARGET_LIFESPAN_MS);
    }
    connect(m_targets[0].timer, &QTimer::timeout, this, &GameWidget::onTarget0Expired);
    connect(m_targets[1].timer, &QTimer::timeout, this, &GameWidget::onTarget1Expired);

    // ── Master render timer (60 Hz) ───────────────────────────────
    connect(&m_renderTimer, &QTimer::timeout, this, &GameWidget::onRenderTick);
    m_renderTimer.setInterval(RENDER_INTERVAL_MS);

    // ── Build initial grid & spawn targets ────────────────────────
    rebuildGrid();
    m_wallClock.start();

    // Target 0 spawns immediately, Target 1 spawns after 1000 ms
    relocateTarget(0);
    m_targets[0].timer->start();

    QTimer::singleShot(1000, this, [this]() {
        relocateTarget(1);
        m_targets[1].timer->start();
    });

    m_renderTimer.start();
}

GameWidget::~GameWidget() = default;

// ================================================================
//  Grid helpers
// ================================================================
void GameWidget::rebuildGrid()
{
    // Thin HUD strip at bottom; leave TIMER_STRIP_H px below each target row
    static constexpr int HUD_H = 36;
    const int playfieldH = height() - HUD_H;

    m_gridCols = width() / CELL_W;
    // Reserve space below the last row for the per-target timer strip
    m_gridRows = (playfieldH - TIMER_STRIP_H) / CELL_H;

    if (m_gridCols < TARGET_COLS * 3) m_gridCols = TARGET_COLS * 3;
    if (m_gridRows < TARGET_ROWS * 2) m_gridRows = TARGET_ROWS * 2;
}

QRect GameWidget::cellRect(int col, int row) const
{
    return { col * CELL_W, row * CELL_H,
             TARGET_COLS * CELL_W, TARGET_ROWS * CELL_H };
}

bool GameWidget::cellsOverlap(int colA, int rowA, int colB, int rowB) const
{
    // Two targets each occupy TARGET_COLS × TARGET_ROWS cells
    return !(colA + TARGET_COLS <= colB || colB + TARGET_COLS <= colA ||
             rowA + TARGET_ROWS <= rowB || rowB + TARGET_ROWS <= rowA);
}

void GameWidget::relocateTarget(int idx)
{
    const int other = 1 - idx;
    const TargetState& otherT = m_targets[other];

    // Max valid top-left origin so the target stays on-screen
    const int maxCol = m_gridCols - TARGET_COLS;
    const int maxRow = m_gridRows - TARGET_ROWS;

    if (maxCol < 0 || maxRow < 0) return; // grid too small

    std::uniform_int_distribution<int> distC(0, maxCol);
    std::uniform_int_distribution<int> distR(0, maxRow);

    int col, row;
    int attempts = 0;
    do {
        col = distC(m_rng);
        row = distR(m_rng);
        ++attempts;
    } while (otherT.isValid() &&
             cellsOverlap(col, row, otherT.gridCol, otherT.gridRow) &&
             attempts < 200);

    m_targets[idx].gridCol     = col;
    m_targets[idx].gridRow     = row;
    m_targets[idx].rect        = cellRect(col, row);
    m_targets[idx].ageFraction = 0.0;
    m_targets[idx].pulsePhase  = 0.0;
}

// ================================================================
//  Slots
// ================================================================
void GameWidget::onRenderTick()
{
    const qreal elapsed = static_cast<qreal>(m_wallClock.elapsed()); // ms

    for (auto& t : m_targets) {
        if (!t.isValid()) continue;

        // ageFraction rises from 0 → 1 over one lifespan; reset by relocate
        const qreal remaining = t.timer->remainingTime();
        t.ageFraction = 1.0 - (remaining / static_cast<qreal>(TARGET_LIFESPAN_MS));
        t.ageFraction = qBound(0.0, t.ageFraction, 1.0);

        // Pulse driven by wall clock so it's independent of lifespan
        t.pulsePhase = std::fmod(elapsed * 0.006 + t.id * M_PI, 2.0 * M_PI);
    }

    update();  // trigger paintEvent
}

void GameWidget::onTarget0Expired()
{
    ++m_misses;
    relocateTarget(0);
    m_targets[0].timer->start(TARGET_LIFESPAN_MS);
}

void GameWidget::onTarget1Expired()
{
    ++m_misses;
    relocateTarget(1);
    m_targets[1].timer->start(TARGET_LIFESPAN_MS);
}

// ================================================================
//  Mouse — shooting
// ================================================================
void GameWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;

    const QPoint click = ev->pos();
    m_crosshair = click;

    for (int i = 0; i < 2; ++i) {
        auto& t = m_targets[i];
        if (!t.isValid()) continue;
        if (!t.rect.contains(click)) continue;

        ++m_score;

        // Bullseye detection: within BULLSEYE_RADIUS pixels of the target centre
        const QPoint centre = t.rect.center();
        const int dx = click.x() - centre.x();
        const int dy = click.y() - centre.y();
        if (dx * dx + dy * dy <= BULLSEYE_RADIUS * BULLSEYE_RADIUS)
            ++m_bullseyes;

        // Immediately relocate and restart the timer
        relocateTarget(i);
        t.timer->start(TARGET_LIFESPAN_MS);
        break;
    }
}

// ================================================================
//  Resize
// ================================================================
void GameWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildGrid();
    // Re-derive screen rects for existing targets
    for (auto& t : m_targets) {
        if (t.isValid())
            t.rect = cellRect(t.gridCol, t.gridRow);
    }
}

// ================================================================
//  paintEvent — master raster draw
// ================================================================
void GameWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // pixel-precise raster

    drawBackground(p);
    drawGrid(p);
    drawScanlines(p);

    for (const auto& t : m_targets)
        if (t.isValid()) drawTarget(p, t);

    drawHUD(p);
}

// ----------------------------------------------------------------
//  Background: two-tone gradient floor + horizon
// ----------------------------------------------------------------
void GameWidget::drawBackground(QPainter& p)
{
    static constexpr int HUD_H = 36;
    const int W  = width();
    const int H  = height();
    const int PH = H - HUD_H;  // playfield height

    // ── Sky / ceiling band ────────────────────────────────────────
    // Draw scanline-by-scanline for a hand-rasterized gradient look
    for (int y = 0; y < PH; ++y) {
        const qreal t = static_cast<qreal>(y) / PH;
        // Interpolate from dark-violet to near-black
        int r = static_cast<int>(0x12 * (1.0 - t) + 0x0D * t);
        int g = static_cast<int>(0x0E * (1.0 - t) + 0x0D * t);
        int b = static_cast<int>(0x2E * (1.0 - t) + 0x2A * t);
        p.setPen(QColor(r, g, b));
        p.drawLine(0, y, W, y);
    }

    // ── Floor / HUD panel ─────────────────────────────────────────
    p.fillRect(0, PH, W, H - PH, COL_FLOOR);

    // Horizon glow line (neon pink strip)
    const QColor horizonGlow { 0xFF, 0x00, 0x99, 160 };
    p.setPen(QPen(horizonGlow, 2));
    p.drawLine(0, PH - 1, W, PH - 1);

    // Perspective grid on floor (vanishing-point lines)
    const int VP_X = W / 2;
    const int VP_Y = PH - 1;
    p.setPen(QPen(QColor(0x2B, 0x00, 0x5E, 120), 1));
    const int floorLines = 18;
    for (int i = 0; i <= floorLines; ++i) {
        int bx = (W * i) / floorLines;
        p.drawLine(VP_X, VP_Y, bx, H);
    }
    // Horizontal floor bands
    p.setPen(QPen(QColor(0x2B, 0x00, 0x5E, 70), 1));
    const int hBands = 10;
    for (int j = 1; j <= hBands; ++j) {
        int fy = PH + (H - PH) * j / hBands;
        p.drawLine(0, fy, W, fy);
    }
}

// ----------------------------------------------------------------
//  Grid: tile outlines with every 4th line brighter
// ----------------------------------------------------------------
void GameWidget::drawGrid(QPainter& p)
{
    static constexpr int HUD_H = 36;
    const int PH = height() - HUD_H;

    for (int col = 0; col <= m_gridCols; ++col) {
        const int x = col * CELL_W;
        const bool major = (col % 4 == 0);
        p.setPen(QPen(major ? COL_GRID_BRIGHT : COL_GRID, 1));
        p.drawLine(x, 0, x, PH);
    }
    for (int row = 0; row <= m_gridRows; ++row) {
        const int y = row * CELL_H;
        const bool major = (row % 4 == 0);
        p.setPen(QPen(major ? COL_GRID_BRIGHT : COL_GRID, 1));
        p.drawLine(0, y, width(), y);
    }
}

// ----------------------------------------------------------------
//  Scanlines: every other horizontal strip is slightly darkened
// ----------------------------------------------------------------
void GameWidget::drawScanlines(QPainter& p)
{
    static constexpr int HUD_H = 36;
    p.setPen(COL_SCANLINE);
    for (int y = 0; y < height() - HUD_H; y += 2) {
        p.drawLine(0, y, width(), y);
    }
}

// ----------------------------------------------------------------
//  Target rasteriser
//
//  Layout (88 × 88 px target, centre at cx,cy, max radius 40):
//
//    Radius range   Colour              Meaning
//    ─────────────  ──────────────────  ──────────────────────────
//    37 … 40        black               outer ring / frame
//    28 … 36        white               ring 4
//    21 … 27        black               ring 3
//    14 … 20        accent (pale)       ring 2
//     9 … 13        dark (near-black)   ring 1
//     0 …  8        accent (bright)     BULLSEYE  ← clickable zone
//
//  Crosshair lines and a pulsing neon border are drawn on top.
//  A countdown timer bar + label is drawn BELOW the target rect.
// ----------------------------------------------------------------
void GameWidget::drawTarget(QPainter& p, const TargetState& t)
{
    const QRect&  r   = t.rect;
    const QColor  acc = TARGET_ACCENT[t.id];

    // Alpha: fully opaque at spawn, dims to 55 % near expiry
    const qreal alpha = 1.0 - 0.45 * t.ageFraction;
    const auto  A = [&](int base) {
        return static_cast<int>(base * alpha);
    };

    const int cx = r.center().x();
    const int cy = r.center().y();
    const int maxR = std::min(r.width(), r.height()) / 2 - 2;

    // ── Concentric ring fill (scanline-by-scanline) ────────────────
    //    We iterate every pixel row inside the target and compute
    //    which ring it belongs to at that y, then fill the horizontal
    //    chord of that ring.
    struct Ring {
        int  outerR;   // inclusive outer radius
        int  innerR;   // exclusive inner radius (next ring starts here)
        QColor colour;
    };

    // Scale ring radii proportionally to maxR (which is ~40 for 88px target)
    const qreal s = maxR / 40.0;
    const Ring rings[] = {
        { maxR,                   static_cast<int>(28*s), QColor(0x10,0x10,0x10, A(240)) },  // frame
        { static_cast<int>(28*s), static_cast<int>(21*s), QColor(0xDD,0xDD,0xDD, A(230)) },  // white
        { static_cast<int>(21*s), static_cast<int>(14*s), QColor(0x18,0x18,0x22, A(240)) },  // black
        { static_cast<int>(14*s), static_cast<int>( 9*s), QColor(acc.red()/2+0x30,
                                                                  acc.green()/2+0x10,
                                                                  acc.blue()/2+0x30, A(200)) }, // accent pale
        { static_cast<int>( 9*s), static_cast<int>( 0*s), QColor(0x08,0x08,0x14, A(240)) },  // inner black
    };

    for (int py = r.top(); py <= r.bottom(); ++py) {
        const int dy2 = (py - cy) * (py - cy);
        for (const auto& ring : rings) {
            const int ro2 = ring.outerR * ring.outerR;
            const int ri2 = ring.innerR * ring.innerR;
            if (dy2 > ro2) continue; // row doesn't reach this ring
            // chord at this y within [innerR, outerR]
            const int xOuter = static_cast<int>(std::sqrt(static_cast<double>(ro2 - dy2)));
            const int xInner = (dy2 < ri2)
                ? static_cast<int>(std::sqrt(static_cast<double>(ri2 - dy2)))
                : 0;
            p.setPen(ring.colour);
            // left half
            p.drawLine(cx - xOuter, py, cx - xInner, py);
            // right half
            p.drawLine(cx + xInner, py, cx + xOuter, py);
        }
    }

    // ── Bullseye (bright accent, matches BULLSEYE_RADIUS) ─────────
    const int bsR = static_cast<int>(BULLSEYE_RADIUS * s);
    QColor bsCol = acc;
    bsCol.setAlpha(A(240));
    for (int py = cy - bsR; py <= cy + bsR; ++py) {
        const int dy2 = (py - cy) * (py - cy);
        if (dy2 > bsR * bsR) continue;
        const int dx = static_cast<int>(std::sqrt(static_cast<double>(bsR * bsR - dy2)));
        p.setPen(bsCol);
        p.drawLine(cx - dx, py, cx + dx, py);
    }

    // ── Crosshair lines (through the full target, clipped to circle) ─
    const QColor chCol(0xFF, 0xFF, 0xFF, A(120));
    p.setPen(chCol);
    p.drawLine(cx - maxR, cy,  cx - bsR - 2, cy);
    p.drawLine(cx + bsR + 2, cy,  cx + maxR, cy);
    p.drawLine(cx, cy - maxR,  cx, cy - bsR - 2);
    p.drawLine(cx, cy + bsR + 2,  cx, cy + maxR);

    // ── Pulsing neon outer border (3-pixel inset) ─────────────────
    const qreal pulse      = 0.55 + 0.45 * std::sin(t.pulsePhase);
    const int   borderAlph = static_cast<int>(255 * pulse * alpha);
    for (int bw = 0; bw < 3; ++bw) {
        QColor bc = acc;
        bc.setAlpha(static_cast<int>(borderAlph * (1.0 - bw * 0.3)));
        p.setPen(bc);
        p.drawLine(r.left()  + bw, r.top()    + bw, r.right() - bw, r.top()    + bw);
        p.drawLine(r.left()  + bw, r.bottom() - bw, r.right() - bw, r.bottom() - bw);
        p.drawLine(r.left()  + bw, r.top()    + bw, r.left()  + bw, r.bottom() - bw);
        p.drawLine(r.right() - bw, r.top()    + bw, r.right() - bw, r.bottom() - bw);
    }

    // ── Corner ticks ──────────────────────────────────────────────
    QColor cc(0xFF, 0xFF, 0xFF, A(200));
    p.setPen(cc);
    const int cs = 5;
    p.drawLine(r.left(),       r.top(),       r.left() + cs, r.top());
    p.drawLine(r.left(),       r.top(),       r.left(),       r.top() + cs);
    p.drawLine(r.right() - cs, r.top(),       r.right(),      r.top());
    p.drawLine(r.right(),      r.top(),       r.right(),      r.top() + cs);
    p.drawLine(r.left(),       r.bottom() - cs, r.left(),     r.bottom());
    p.drawLine(r.left(),       r.bottom(),    r.left() + cs,  r.bottom());
    p.drawLine(r.right() - cs, r.bottom(),    r.right(),      r.bottom());
    p.drawLine(r.right(),      r.bottom() - cs, r.right(),    r.bottom());

    // ── Timer strip below the target ──────────────────────────────
    const int rem    = t.timer->remainingTime();
    const int barY   = r.bottom() + 3;
    const int barH   = 4;
    const int barW   = r.width();
    const int filled = static_cast<int>(barW * rem / TARGET_LIFESPAN_MS);

    // trough
    p.fillRect(r.left(), barY, barW, barH, QColor(0x22, 0x11, 0x22, 140));
    // filled portion – colour shifts red as time runs out
    const qreal tFrac = rem / static_cast<qreal>(TARGET_LIFESPAN_MS);
    QColor barCol(
        static_cast<int>(0xFF * (1.0 - tFrac) + acc.red()   * tFrac),
        static_cast<int>(0x22 * (1.0 - tFrac) + acc.green() * tFrac),
        static_cast<int>(0x22 * (1.0 - tFrac) + acc.blue()  * tFrac),
        A(180)
    );
    p.fillRect(r.left(), barY, filled, barH, barCol);

    // Label: "T1  1.4s" in tiny font, right-aligned above bar
    QFont lblFont("Courier", 7);
    p.setFont(lblFont);
    QColor lblCol = acc;
    lblCol.setAlpha(A(190));
    p.setPen(lblCol);
    const qreal secs = rem / 1000.0;
    p.drawText(r.left(), barY - 1,
               r.width(), 10,
               Qt::AlignRight | Qt::AlignBottom,
               QString("T%1 %2s").arg(t.id + 1).arg(secs, 0, 'f', 1));
}

// ----------------------------------------------------------------
//  HUD panel — narrow strip: HITS · BULLSEYES · MISSES only
// ----------------------------------------------------------------
void GameWidget::drawHUD(QPainter& p)
{
    static constexpr int HUD_H = 36;
    const int W   = width();
    const int H   = height();
    const int PH  = H - HUD_H;

    // HUD backdrop: single dark scanline-rasterized band
    for (int y = PH; y < H; ++y) {
        const qreal f = static_cast<qreal>(y - PH) / HUD_H;
        p.setPen(QColor(
            static_cast<int>(0x0A * (1.0 - f) + 0x05 * f),
            static_cast<int>(0x08 * (1.0 - f) + 0x03 * f),
            static_cast<int>(0x1C * (1.0 - f) + 0x10 * f)
        ));
        p.drawLine(0, y, W, y);
    }

    // Top divider glow
    p.setPen(QPen(QColor(0x00, 0xFF, 0xC8, 160), 1));
    p.drawLine(0, PH, W, PH);

    // ── Stat text (single centred line) ──────────────────────────
    QFont font("Courier", 11, QFont::Bold);
    p.setFont(font);
    const int ty = PH + 22;

    // HITS
    p.setPen(QColor(0x00, 0xFF, 0xC8));
    p.drawText(24, ty, QString("HITS  %1").arg(m_score, 4, 10, QChar('0')));

    // BULLSEYES (gold accent)
    p.setPen(QColor(0xFF, 0xD7, 0x00));
    const int midX = W / 2;
    p.drawText(midX - 70, ty, QString("BULLSEYES  %1").arg(m_bullseyes, 3, 10, QChar('0')));

    // MISSES (dim red)
    p.setPen(QColor(0xFF, 0x44, 0x44));
    p.drawText(W - 150, ty, QString("MISSES  %1").arg(m_misses, 4, 10, QChar('0')));
}

