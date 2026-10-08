#pragma once
// The deck's control surface: trimixxx2's top plate, every control the S3
// reads and every light it drives. The Pi's screen is QEMU's own window.

#include <QWidget>

class Machine;
class VirtualS3;
class Sticks;
class Wiring;
class QLabel;
class QVBoxLayout;

class DeckWindow : public QWidget {
    Q_OBJECT
public:
    DeckWindow(Machine* m, VirtualS3* s3, Sticks* sticks, const Wiring* wiring, QWidget* parent = nullptr);

signals:
    void powerOnRequested();

private:
    void refreshSticks();

    Machine*      m_machine;
    VirtualS3*           m_s3;
    Sticks*       m_sticks;
    const Wiring* m_wiring;
    QVBoxLayout*  m_stickRows = nullptr;
    QLabel*       m_status = nullptr;
};
