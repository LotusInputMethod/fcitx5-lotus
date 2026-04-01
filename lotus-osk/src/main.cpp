#include <QApplication>
#include "osk_controller.h"
#include "osk_window.h"

int main(int argc, char* argv[]) {
    // Use QApplication since we are using QWidget
    QApplication app(argc, argv);
    app.setApplicationName("Lotus OSK");
    app.setOrganizationName("Lotus");

    OSKController controller;

    // The controller will manage the OSKWindow
    controller.showWindow();

    return app.exec();
}
