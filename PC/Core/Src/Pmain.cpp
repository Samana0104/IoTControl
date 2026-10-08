#include "Pmainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication qmain(argc, argv);
    qmain.setOrganizationName(QStringLiteral("IoTControl"));
    qmain.setApplicationName(QStringLiteral("IoTControl_PC"));
    qmain.setStyle(QStringLiteral("Fusion"));

    MainWindow mw;
    mw.show();

    return qmain.exec();
}
