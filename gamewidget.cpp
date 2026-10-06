#include "gamewidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QStandardPaths>
#include <QApplication>
#include <algorithm>
#include <cmath>

namespace {
const QColor ink("#514c44"), muted("#827b70"), paper("#faf8f2"), border("#cfc5b7");
const std::array<QColor, 3> colors {{QColor("#538c66"), QColor("#528ac4"), QColor("#c4543e")}};
const std::array<int, 3> points {{10, 1, -5}};
const std::array<QString, 3> names {{"Green", "Blue", "Red"}};
void label(QPainter& p, QRectF rect, QString text, int size = 14, QColor color = ink, bool bold = false, int alignment = Qt::AlignLeft | Qt::AlignVCenter) {
    p.setPen(color);
    QFont font("Helvetica Neue", size); font.setBold(bold); p.setFont(font);
    p.drawText(rect, alignment, text);
}
void card(QPainter& p, QRectF rect) {
    p.setPen(QPen(border, 1)); p.setBrush(QColor("#f8f4eb")); p.drawRoundedRect(rect, 18, 18);
}
}
GameWidget::GameWidget(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Pixel Reflex — focus before you click");
    resize(1280, 820); setMinimumSize(960, 740); setFocusPolicy(Qt::StrongFocus);
    m_pause = new QPushButton("Pause", this); m_restart = new QPushButton("Restart", this);
    QString style = "QPushButton { background: #557e68; color: white; border: none; border-radius: 10px; font-size: 15px; font-weight: 600; } QPushButton:hover { background: #456b56; }";
    m_pause->setStyleSheet(style);
    m_restart->setStyleSheet("QPushButton { background: #eee6d9; color: #514c44; border: 1px solid #cfc5b7; border-radius: 10px; font-size: 15px; } QPushButton:hover { background: #e3d8c7; }");
    connect(m_pause, &QPushButton::clicked, this, &GameWidget::togglePause);
    connect(m_restart, &QPushButton::clicked, this, &GameWidget::reset);
    m_sound = new QPushButton("Sound on", this);
    m_sound->setStyleSheet("QPushButton { border: 1px solid #cfc5b7; border-radius: 5px; color: #514c44; background: #eee6d9; font-size: 11px; }");
    connect(m_sound, &QPushButton::clicked, this, &GameWidget::toggleSound);
#ifdef Q_OS_MACOS
    m_audioPlayer = QStandardPaths::findExecutable("afplay");
#else
    m_audioPlayer = QStandardPaths::findExecutable("paplay");
    if (m_audioPlayer.isEmpty()) m_audioPlayer = QStandardPaths::findExecutable("aplay");
#endif
    for (int k = 0; k < 3; ++k) {
        m_sounds[k] = new QProcess(this);
        QString name = names[k].toLower() + ".wav";
        QFile::copy(":/sounds/" + name, m_audioDirectory.filePath(name));
        connect(m_sounds[k], &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (!m_muted && error == QProcess::FailedToStart) QApplication::beep();
        });
    }
    m_clock.start(); layoutBoard(); reset();
    m_timer.setTimerType(Qt::PreciseTimer); m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &GameWidget::tick); m_timer.start();
}
GameWidget::~GameWidget() {
    for (auto sound : m_sounds) {
        sound->disconnect(this);
        if (sound->state() != QProcess::NotRunning) {
            sound->kill(); sound->waitForFinished(1000);
        }
    }
}
int GameWidget::level() const { return std::min(10, m_peak / 20 + 1); }
int GameWidget::interval() const { return std::max(260, 1000 - (level() - 1) * 85); }
QRectF GameWidget::cell(int col, int row) const { return QRectF(m_board.x() + col * m_cell, m_board.y() + row * m_cell, m_cell, m_cell); }
void GameWidget::layoutBoard() {
    m_cols = std::max(1, (width() - 378) / m_cell); m_rows = std::max(1, (height() - 40) / m_cell);
    m_board = QRectF(20, 20, m_cols * m_cell, m_rows * m_cell);
    m_targets.erase(std::remove_if(m_targets.begin(), m_targets.end(), [this](const TargetState& t) { return t.col >= m_cols || t.row >= m_rows; }), m_targets.end());
    m_sound->setGeometry(width() - 115, 81, 72, 25);
    m_pause->setGeometry(width() - 316, height() - 78, 134, 44);
    m_restart->setGeometry(width() - 170, height() - 78, 134, 44);
}
void GameWidget::resizeEvent(QResizeEvent*) { layoutBoard(); }
void GameWidget::reset() {
    m_score = m_peak = m_expired = 0; m_hits = {{0, 0, 0}};
    m_now = 0; m_last = m_clock.elapsed(); m_nextSpawn = 650;
    m_effects.clear(); m_redAlert = -10000;
    for (auto sound : m_sounds) sound->kill();
    m_targets.clear(); m_history.clear(); m_paused = false; m_pause->setText("Pause"); spawn(); update();
}
void GameWidget::toggleSound() {
    m_muted = !m_muted; m_sound->setText(m_muted ? "Muted" : "Sound on");
    if (m_muted) for (auto sound : m_sounds) sound->kill();
}
void GameWidget::togglePause() {
    if (!m_paused) for (auto sound : m_sounds) sound->kill();
    m_paused = !m_paused; m_last = m_clock.elapsed(); m_pause->setText(m_paused ? "Resume" : "Pause"); update();
}
void GameWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) togglePause();
    else if (e->key() == Qt::Key_M && !e->isAutoRepeat()) toggleSound();
    else if (e->key() == Qt::Key_R) reset();
    else QWidget::keyPressEvent(e);
}
void GameWidget::spawn() {
    if (m_targets.size() >= 5) return;
    std::uniform_int_distribution<int> col(0, m_cols - 1), row(0, m_rows - 1), roll(0, 99);
    for (int attempt = 0; attempt < 100; ++attempt) {
        int c = col(m_rng), r = row(m_rng);
        if (std::any_of(m_targets.begin(), m_targets.end(), [=](const TargetState& t) { return t.col == c && t.row == r; })) continue;
        int chance = roll(m_rng);
        // Rare rewards, common safe targets, unpredictable short-lived hazards.
        int kind = chance < 12 ? 0 : chance < 76 ? 1 : 2;
        int lifetime = std::max(750, 2300 - (level() - 1) * 155);
        if (kind == 2) lifetime = std::max(600, lifetime * 3 / 4);
        m_targets.push_back({c, r, kind, m_now, lifetime}); return;
    }
}
void GameWidget::tick() {
    qint64 time = m_clock.elapsed(), delta = time - m_last; m_last = time;
    if (m_paused) return;
    m_now += std::min<qint64>(delta, 100); // Avoid a burst after the app resumes from a stall.
    m_targets.erase(std::remove_if(m_targets.begin(), m_targets.end(), [this](const TargetState& t) {
        if (m_now - t.born < t.lifetime) return false;
        if (t.kind != 2) ++m_expired;
        return true;
    }), m_targets.end());
    if (m_now >= m_nextSpawn) { spawn(); m_nextSpawn = m_now + interval(); }
    m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(), [this](const HitEffect& effect) {
        return m_now - effect.time >= (effect.kind == 0 ? 650 : 450);
    }), m_effects.end());
    update();
}
void GameWidget::mousePressEvent(QMouseEvent* e) {
    if (m_paused || e->button() != Qt::LeftButton) return;
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
        if (!cell(it->col, it->row).contains(e->position())) continue;
        m_effects.push_back({it->kind, m_now, cell(it->col, it->row).center()});
        if (it->kind == 2) m_redAlert = m_now;
        if (!m_muted) {
            if (m_audioPlayer.isEmpty()) QApplication::beep();
            else if (m_sounds[it->kind]->state() == QProcess::NotRunning)
                m_sounds[it->kind]->start(m_audioPlayer, {m_audioDirectory.filePath(names[it->kind].toLower()+".wav")});
        }
        m_score += points[it->kind]; ++m_hits[it->kind];
        m_peak = std::max(m_peak, m_score);
        m_history.insert(m_history.begin(), {it->kind, m_score, m_now, cell(it->col, it->row).center()});
        if (m_history.size() > 6) m_history.pop_back();
        m_targets.erase(it);
        m_nextSpawn = std::min(m_nextSpawn, m_now + interval()); update(); return;
    }
}
void GameWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#e8e1d6")); p.fillRect(m_board, paper);
    // Square dots at cell corners, like the reference's pixel-paper board.
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int c = 0; c <= m_cols; ++c)
        for (int r = 0; r <= m_rows; ++r)
            p.fillRect(QRectF(m_board.x()+c*m_cell-1.5, m_board.y()+r*m_cell-1.5, 3, 3), QColor("#45443f"));
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& t : m_targets) {
        qreal age = m_now - t.born;
        qreal opacity = std::min(1.0, age / 90.0) * std::min(1.0, (t.lifetime - age) / 140.0);
        p.setOpacity(opacity); QRectF box = cell(t.col,t.row).adjusted(1,1,-1,-1);
        p.fillRect(box.adjusted(2,2,-2,-2), colors[t.kind]);
        p.setPen(QPen(colors[t.kind].darker(150), 2)); p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(2,2,-2,-2));
        p.fillRect(QRectF(box.left()+5,box.top()+5,7,3), QColor(255,255,255,100));
        p.fillRect(QRectF(box.left(),box.bottom()-3,box.width() * (1.0 - age/t.lifetime),3), QColor(255,255,255,190));
        p.setOpacity(1);
    }
    p.save(); p.setClipRect(m_board);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const auto& effect : m_effects) {
        qreal duration = effect.kind == 0 ? 650.0 : 450.0;
        qreal progress = std::clamp((m_now-effect.time)/duration, 0.0, 1.0);
        qreal radius = (effect.kind == 0 ? 68 : 38) * std::sqrt(progress);
        p.setOpacity(1-progress);
        p.setPen(QPen(colors[effect.kind], 2)); p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(effect.position.x()-radius*.65,effect.position.y()-radius*.65,radius*1.3,radius*1.3));
        int count = effect.kind == 0 ? 16 : 10;
        for (int i=0; i<count; ++i) {
            qreal angle = i * 6.28318530718 / count;
            QPointF pos = effect.position + QPointF(std::cos(angle),std::sin(angle))*radius;
            qreal size = (effect.kind == 0 ? 7 : 5)*(1-progress*.6);
            p.fillRect(QRectF(pos.x()-size/2,pos.y()-size/2,size,size),i%3==0 ? colors[effect.kind].lighter(150) : colors[effect.kind]);
        }
        if (progress < .25) {
            qreal size = 18*(1-progress/.25);
            p.fillRect(QRectF(effect.position.x()-size/2,effect.position.y()-size/2,size,size),Qt::white);
        }
    }
    p.restore();
    qreal alertAge = m_now - m_redAlert;
    if (alertAge >= 0 && alertAge < 850) {
        qreal strength = 1-alertAge/850;
        p.fillRect(m_board,QColor(196,54,40,int(45*strength)));
        p.setPen(QPen(QColor(196,54,40,int(230*strength)),5)); p.setBrush(Qt::NoBrush);
        p.drawRect(m_board.adjusted(3,3,-3,-3));
        QRectF warning(m_board.center().x()-135,m_board.top()+18,270,44);
        p.fillRect(warning,QColor("#b6382c"));
        label(p,warning,"RED ALERT   −5 POINTS",15,Qt::white,true,Qt::AlignCenter);
    }
    for (const auto& h : m_history) {
        qreal age = m_now - h.time;
        if (age > 650) continue;
        p.setOpacity(1.0-age/650);
        label(p, QRectF(h.position.x()-34,h.position.y()-32-age/30,68,30), QString("%1%2").arg(points[h.kind]>0?"+":"").arg(points[h.kind]), 17, colors[h.kind], true, Qt::AlignCenter);
        p.setOpacity(1);
    }
    qreal x = width()-336;
    card(p, QRectF(x,20,316,height()-40));
    label(p,{x+24,40,268,34},"Pixel Reflex",24,ink,true);
    label(p,{x+24,79,185,26},"Look. Decide. Click.",14,muted);
    label(p,{x+24,119,268,20},"TOTAL SCORE",11,muted,true);
    label(p,{x+24,140,268,65},QString::number(m_score),42,ink,true);
    label(p,{x+24,208,268,25},QString("Level %1   ·   %2 boxes / second").arg(level()).arg(1000.0/interval(),0,'f',1),13);
    label(p,{x+24,235,268,22},"Speed rises every 20 points earned.",12,muted);
    label(p,{x+24,269,268,25},"Score calculation",16,ink,true);
    for (int k=0; k<3; ++k) {
        qreal y=310+k*48;
        p.fillRect(QRectF(x+24,y+7,18,18),colors[k]);
        label(p,{x+52,y,110,32},QString("%1  %2%3").arg(names[k]).arg(points[k]>0?"+":"").arg(points[k]),14);
        label(p,{x+162,y,130,32},QString("%1 × %2 = %3").arg(m_hits[k]).arg(points[k]).arg(m_hits[k]*points[k]),13,ink,false,Qt::AlignRight|Qt::AlignVCenter);
    }
    p.setPen(border); p.drawLine(QPointF(x+24,458),QPointF(x+292,458));
    label(p,{x+24,471,268,24},QString("%1 + %2 − %3 = %4").arg(m_hits[0]*10).arg(m_hits[1]).arg(m_hits[2]*5).arg(m_score),16,ink,true);
    label(p,{x+24,505,268,22},QString("Missed rewards: %1").arg(m_expired),12,muted);
    label(p,{x+24,542,268,24},"Recent clicks",14,ink,true);
    for (int i=0; i<std::min({3, int(m_history.size()), std::max(0, (height() - 740) / 25)}); ++i) {
        const auto& h=m_history[i];
        label(p,{x+24,574.0+i*25,268,24},QString("%1  %2%3   →   %4").arg(names[h.kind]).arg(points[h.kind]>0?"+":"").arg(points[h.kind]).arg(h.total),13,colors[h.kind]);
    }
    label(p,{x+24,height()-166.0,268,52},"Green is rare. Red is a surprise.\nLet red disappear; keep your focus.",12,muted);
    label(p,{x+24,height()-111.0,268,23},"Space to pause  ·  R to restart",11,muted);
    if (m_paused) {
        p.fillRect(m_board,QColor(250,248,242,220));
        label(p,m_board,"Paused\nPress Space or Resume",24,ink,true,Qt::AlignCenter);
    }
}
