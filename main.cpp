// ============================================================
//  main.cpp — FPS Target Shooter (Qt / C++)
//  CG Lab Project — Phase 1
// ============================================================

#include <QApplication>
#include "gamewidget.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Pixel Reflex");
    app.setApplicationVersion("2.0.0 — Pixel Reflex");

    GameWidget window;
    window.show();

    return app.exec();
}
