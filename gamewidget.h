#pragma once
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QProcess>
#include <QTemporaryDir>
#include <array>
#include <random>
#include <vector>

struct TargetState {
    int col, row, kind;
    qint64 born, lifetime;
};
struct ScoreEvent {
    int kind, total;
    qint64 time;
    QPointF position;
};
struct HitEffect {
    int kind;
    qint64 time;
    QPointF position;
};
class GameWidget : public QWidget {
    Q_OBJECT
public:
    explicit GameWidget(QWidget* parent = nullptr);
    ~GameWidget() override;
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    void tick();
    void reset();
    void togglePause();
    void toggleSound();
    void layoutBoard();
    void spawn();
    int level() const;
    int interval() const;
    QRectF cell(int col, int row) const;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0, m_now = 0, m_nextSpawn = 0;
    bool m_paused = false, m_muted = false;
    qint64 m_redAlert = -10000;
    std::array<QProcess*, 3> m_sounds;
    QTemporaryDir m_audioDirectory;
    QString m_audioPlayer;
    std::vector<HitEffect> m_effects;
    QRectF m_board;
    int m_cols = 0, m_rows = 0, m_cell = 32;
    int m_score = 0, m_peak = 0, m_expired = 0;
    std::array<int, 3> m_hits {{0, 0, 0}};
    std::vector<TargetState> m_targets;
    std::vector<ScoreEvent> m_history;
    std::mt19937 m_rng {std::random_device{}()};
    QPushButton *m_pause, *m_restart, *m_sound;
};
