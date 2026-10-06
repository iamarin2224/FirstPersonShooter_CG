// ============================================================
//  gamewidget.h — Phase 1: FPS Target Shooter (Qt / C++)
//  CG Lab Project — 2D Rasterized Arcade Shooter
// ============================================================
#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QRect>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <array>
#include <random>

// ------------------------------------------------------------------
//  Grid / rendering constants
// ------------------------------------------------------------------
static constexpr int   CELL_W      = 44;   // pixels per grid column
static constexpr int   CELL_H      = 44;   // pixels per grid row
static constexpr int   TARGET_COLS = 2;    // cells a target spans horizontally
static constexpr int   TARGET_ROWS = 2;    // cells a target spans vertically

// Bullseye: a tiny dead-centre circle (radius in pixels) that scores double
static constexpr int   BULLSEYE_RADIUS     = 8;    // px — must match drawTarget ring

static constexpr int   TARGET_LIFESPAN_MS  = 2000; // ms before a target relocates
static constexpr int   RENDER_INTERVAL_MS  = 16;   // ~60 FPS master tick
// Vertical space reserved below each target for the countdown timer bar + label
static constexpr int   TIMER_STRIP_H       = 18;

// ------------------------------------------------------------------
//  TargetState — full lifecycle descriptor for one active target
// ------------------------------------------------------------------
struct TargetState
{
    // Position expressed as top-left grid cell index
    int  gridCol  { -1 };
    int  gridRow  { -1 };

    // Screen-space bounding rect (derived from grid coords)
    QRect rect;

    // Per-target independent countdown
    QTimer* timer { nullptr };

    // Visual phase: 0.0 (just spawned / fully opaque) … 1.0 (about to relocate)
    // Driven by elapsedMs / TARGET_LIFESPAN_MS
    qreal  ageFraction { 0.0 };

    // Pulse / shimmer animation phase [0, 2π)
    qreal  pulsePhase  { 0.0 };

    // Unique index (0 or 1) so each target can pick a distinct colour accent
    int  id { 0 };

    bool isValid() const { return gridCol >= 0; }
};

// ------------------------------------------------------------------
//  GameWidget — primary rendering + game-logic widget
// ------------------------------------------------------------------
class GameWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GameWidget(QWidget* parent = nullptr);
    ~GameWidget() override;

protected:
    // Qt overrides
    void paintEvent(QPaintEvent* event)    override;
    void resizeEvent(QResizeEvent* event)  override;
    void mousePressEvent(QMouseEvent* ev)  override;

private slots:
    // Called by master 60 Hz render timer
    void onRenderTick();

    // Called by each target's independent 2-second lifecycle timer
    void onTarget0Expired();
    void onTarget1Expired();

private:
    // ---- grid helpers ----
    void  rebuildGrid();
    QRect cellRect(int col, int row) const;
    void  relocateTarget(int idx);

    // ---- raster drawing routines ----
    void drawBackground(QPainter& p);
    void drawGrid(QPainter& p);
    void drawScanlines(QPainter& p);
    void drawHUD(QPainter& p);
    void drawTarget(QPainter& p, const TargetState& t);

    // ---- utility ----
    bool cellsOverlap(int colA, int rowA, int colB, int rowB) const;

    // ---- state ----
    std::array<TargetState, 2> m_targets;

    QTimer        m_renderTimer;
    QElapsedTimer m_wallClock;      // wall time for visual animation

    // Grid dimensions (recalculated on resize)
    int m_gridCols { 0 };
    int m_gridRows { 0 };

    // Score / HUD
    int m_score     { 0 };  // total target hits (ring + bullseye)
    int m_bullseyes { 0 };  // subset of hits that landed in the bullseye zone
    int m_misses    { 0 };  // targets that expired without being shot

    // RNG
    std::mt19937 m_rng;

    // Crosshair position
    QPoint m_crosshair;
};
