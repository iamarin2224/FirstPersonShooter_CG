// ============================================================
//  main.cpp — FPS Target Shooter (Qt / C++)
//  CG Lab Project — Phase 1
// ============================================================

#include <QApplication>
#include "gamewidget.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FPS Training Range");
    app.setApplicationVersion("1.0.0 — Phase 1");

    GameWidget window;
    window.show();

    return app.exec();
}
