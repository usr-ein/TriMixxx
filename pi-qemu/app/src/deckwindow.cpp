#include "deckwindow.h"

#include "MidiMap.hpp"
#include "machine.h"
#include "s3.h"
#include "sticks.h"
#include "wiring.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

namespace {

const QColor kPlate(24, 24, 26), kOff(48, 48, 52), kText(200, 200, 205);

// ---- a button the S3 reads, with whatever light it has -----------------------
class DeckButton : public QWidget {
public:
    std::function<void(bool)> onPress;
    std::function<QColor(int led)> light; // LED 0/1 colour, invalid = off
    QString label;
    bool round = true, held = false;

    DeckButton(QWidget* parent, QString text, int diameter) : QWidget(parent), label(std::move(text)) {
        setFixedSize(diameter + 30, diameter + 18);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int d = height() - 18;
        QRectF r((width() - d) / 2.0, 1, d - 2, d - 2);
        QColor a = light ? light(0) : QColor(), b = light ? light(1) : QColor();
        if (!a.isValid() || a == Qt::black) a = kOff;
        if (!b.isValid() || b == Qt::black) b = kOff;
        if (a == b) {
            p.setBrush(a);
            p.setPen(QPen(held ? Qt::white : QColor(90, 90, 95), held ? 2.5 : 1.2));
            p.drawEllipse(r);
        } else { // the OneButton boards' two LEDs, side by side
            p.setPen(Qt::NoPen);
            p.setBrush(a);
            p.drawPie(r, 90 * 16, 180 * 16);
            p.setBrush(b);
            p.drawPie(r, 270 * 16, 180 * 16);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(held ? Qt::white : QColor(90, 90, 95), held ? 2.5 : 1.2));
            p.drawEllipse(r);
        }
        p.setPen(kText);
        QFont f = font();
        f.setPointSizeF(8);
        p.setFont(f);
        p.drawText(QRect(0, d, width(), 18), Qt::AlignCenter, label);
    }
    void mousePressEvent(QMouseEvent* e) override {
        // Shift-click latches it down: two held at once, as the panic chord needs.
        if (held && (e->modifiers() & Qt::ShiftModifier)) { held = false; if (onPress) onPress(false); update(); return; }
        held = true;
        if (onPress) onPress(true);
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->modifiers() & Qt::ShiftModifier) return; // latched
        held = false;
        if (onPress) onPress(false);
        update();
    }
};

// ---- the jog wheel -----------------------------------------------------------
// The platter's top is touch-sensitive (scratch); its rim is not (pitch bend).
// One turn is 12960 ticks, as the optical encoder counts them.
class JogWheel : public QWidget {
public:
    std::function<void(int)> onTicks;
    std::function<void(bool)> onTouch;

    explicit JogWheel(QWidget* parent) : QWidget(parent) { setMinimumSize(260, 260); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = rect().center();
        const double R = qMin(width(), height()) / 2.0 - 4;
        p.setPen(QPen(QColor(110, 110, 115), 3));
        p.setBrush(QColor(36, 36, 40));
        p.drawEllipse(c, R, R);
        p.setBrush(m_touching ? QColor(70, 70, 80) : QColor(20, 20, 22));
        p.drawEllipse(c, R * 0.72, R * 0.72);
        p.setPen(QPen(QColor(230, 230, 235), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c, c + QPointF(qSin(m_angle), -qCos(m_angle)) * R * 0.68);
        p.setPen(kText);
        p.drawText(rect().adjusted(0, 0, 0, -int(R * 0.25)), Qt::AlignHCenter | Qt::AlignBottom,
                   m_touching ? "SCRATCH" : "drag the top to scratch, the rim to bend");
    }
    void mousePressEvent(QMouseEvent* e) override {
        m_last = angleAt(e->position());
        const double R = qMin(width(), height()) / 2.0;
        m_touching = QLineF(rect().center(), e->position()).length() < R * 0.72;
        if (m_touching && onTouch) onTouch(true);
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        double a = angleAt(e->position()), d = a - m_last;
        if (d > M_PI) d -= 2 * M_PI;
        if (d < -M_PI) d += 2 * M_PI;
        m_last = a;
        turn(d);
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (m_touching && onTouch) onTouch(false);
        m_touching = false;
        update();
    }
    void wheelEvent(QWheelEvent* e) override { // a nudge on the rim
        turn(-e->angleDelta().y() / 120.0 * (2 * M_PI / 64));
    }

private:
    double angleAt(QPointF p) const {
        QPointF v = p - rect().center();
        return qAtan2(v.x(), -v.y());
    }
    void turn(double radians) {
        m_angle += radians;
        m_frac += radians / (2 * M_PI) * midimapTicks;
        int t = int(m_frac);
        m_frac -= t;
        if (t && onTicks) onTicks(t);
        update();
    }
    static constexpr double midimapTicks = 12960; // JogWheel::TICKS_PER_REV
    double m_angle = 0, m_last = 0, m_frac = 0;
    bool m_touching = false;
};

// ---- the track encoder: turn by scrolling over it, push by clicking -----------
class Encoder : public QWidget {
public:
    std::function<void(int)> onTurn;
    std::function<void(bool)> onPush;
    explicit Encoder(QWidget* parent) : QWidget(parent) { setFixedSize(70, 86); setCursor(Qt::PointingHandCursor); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(8, 2, 54, 54);
        p.setBrush(m_pushed ? QColor(90, 90, 100) : QColor(40, 40, 44));
        p.setPen(QPen(QColor(120, 120, 125), 2));
        p.drawEllipse(r);
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(r.center(), r.center() + QPointF(qSin(m_pos), -qCos(m_pos)) * 22);
        p.setPen(kText);
        QFont f = font(); f.setPointSizeF(8); p.setFont(f);
        p.drawText(QRect(0, 58, width(), 28), Qt::AlignCenter, "BROWSE\nscroll / click");
    }
    void wheelEvent(QWheelEvent* e) override {
        m_acc += e->angleDelta().y();
        int detents = m_acc / 120;
        m_acc -= detents * 120;
        if (detents && onTurn) { onTurn(detents); m_pos += detents * 0.4; update(); }
    }
    void mousePressEvent(QMouseEvent*) override { m_pushed = true; if (onPush) onPush(true); update(); }
    void mouseReleaseEvent(QMouseEvent*) override { m_pushed = false; if (onPush) onPush(false); update(); }

private:
    int m_acc = 0;
    double m_pos = 0;
    bool m_pushed = false;
};

} // namespace

DeckWindow::DeckWindow(Machine* m, S3* s3, Sticks* sticks, const Wiring* wiring, QWidget* parent)
    : QWidget(parent), m_machine(m), m_s3(s3), m_sticks(sticks), m_wiring(wiring) {
    setWindowTitle("pi-qemu — " + wiring->deck() + " controls");
    QPalette pal = palette();
    pal.setColor(QPalette::Window, kPlate);
    pal.setColor(QPalette::WindowText, kText);
    setPalette(pal);
    setAutoFillBackground(true);

    auto pressFor = [this](const QString& control) {
        return [this, control](bool down) {
            if (auto n = m_wiring->note(control)) m_s3->button(*n, down);
        };
    };
    auto padButton = [&](int ring, int node) {
        const int base = ring == 0 ? midimap::PAD_A_BASE : midimap::PAD_B_BASE;
        QString label = m_wiring->padLabel(ring, node);
        auto* b = new DeckButton(this, label.isEmpty() ? "—" : label, 38);
        b->onPress = [this, note = quint8(base + node)](bool down) { m_s3->button(note, down); };
        b->light = [this, ring, node](int led) {
            return (ring == 0 ? m_s3->leds().ringA : m_s3->leds().ringB)[node][led];
        };
        return b;
    };
    auto lamp = [&](const QString& text, const QString& control, int size, std::function<bool()> lit, QColor on) {
        auto* b = new DeckButton(this, text, size);
        b->onPress = pressFor(control);
        if (lit) b->light = [lit, on](int) { return lit() ? on : QColor(); };
        return b;
    };

    // Ring A down the left, ring B along the top: pads by node, in chain order.
    auto* ringA = new QVBoxLayout;
    for (int i = 0; i < m_wiring->ringSize(0); i++) ringA->addWidget(padButton(0, i));
    ringA->addStretch();
    auto* ringB = new QHBoxLayout;
    for (int i = 0; i < m_wiring->ringSize(1); i++) ringB->addWidget(padButton(1, i));
    ringB->addStretch();

    auto* jog = new JogWheel(this);
    jog->onTicks = [this](int t) { m_s3->jog(m_wiring->jogReversed() ? -t : t); };
    jog->onTouch = pressFor("jog-touch");

    auto* enc = new Encoder(this);
    enc->onTurn = [this](int d) { m_s3->encoder(d); };
    enc->onPush = pressFor("push");

    // The 14-bit tempo fader; double-click puts it back on the centre detent.
    auto* fader = new QSlider(Qt::Vertical, this);
    fader->setRange(0, 16383);
    fader->setValue(8192);
    fader->setMinimumHeight(220);
    connect(fader, &QSlider::valueChanged, this, [this](int v) { m_s3->tempo(v); });
    auto* faderBox = new QVBoxLayout;
    auto* faderLabel = new QLabel("TEMPO");
    faderLabel->setToolTip("double-click the label: back to the centre detent");
    faderBox->addWidget(faderLabel, 0, Qt::AlignHCenter);
    faderBox->addWidget(fader, 1, Qt::AlignHCenter);
    auto* center = new QPushButton("centre");
    connect(center, &QPushButton::clicked, fader, [fader] { fader->setValue(8192); });
    faderBox->addWidget(center);

    const DeckLeds* L = &m_s3->leds();
    auto* loops = new QHBoxLayout;
    loops->addWidget(lamp("IN", "loop-in", 30, [L] { return L->loopIn; }, QColor(255, 160, 0)));
    loops->addWidget(lamp("OUT", "loop-out", 30, [L] { return L->loopOut; }, QColor(255, 160, 0)));
    loops->addWidget(lamp("RELOOP", "reloop", 20, {}, {}));
    loops->addStretch();
    auto* transport = new QVBoxLayout;
    transport->addLayout(loops);
    transport->addWidget(lamp("CUE", "cue", 66, [L] { return L->cue; }, QColor(255, 150, 0)));
    transport->addWidget(lamp("PLAY / PAUSE", "play", 66, [L] { return L->play; }, QColor(60, 220, 60)));

    auto* deckTop = new QHBoxLayout;
    deckTop->addLayout(ringB, 1);
    deckTop->addWidget(enc);
    auto* middle = new QHBoxLayout;
    middle->addLayout(ringA);
    middle->addWidget(jog, 1);
    middle->addLayout(faderBox);
    auto* plate = new QVBoxLayout;
    plate->addLayout(deckTop);
    plate->addLayout(middle, 1);
    plate->addLayout(transport);

    // The side: the S3's status LED, the board, the USB sticks.
    auto* side = new QVBoxLayout;
    auto* s3led = new QLabel("●  S3");
    side->addWidget(s3led);
    connect(m_s3, &S3::linkActivity, s3led, [s3led](bool tx) {
        s3led->setStyleSheet(tx ? "color: rgb(0,200,0)" : "color: rgb(220,200,0)");
        QTimer::singleShot(80, s3led, [s3led] { s3led->setStyleSheet("color: rgb(140,0,140)"); });
    });
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setMinimumWidth(260);
    side->addWidget(m_status);
    auto* powerRow = new QHBoxLayout;
    auto* on = new QPushButton("Power on");
    auto* off = new QPushButton("Pull the plug");
    off->setToolTip("Like the deck's mains switch: no shutdown. Use the skin's POWER menu for a clean one.");
    connect(on, &QPushButton::clicked, this, &DeckWindow::powerOnRequested);
    connect(off, &QPushButton::clicked, this, [this] { m_machine->pullPlug(); });
    powerRow->addWidget(on);
    powerRow->addWidget(off);
    side->addLayout(powerRow);
    side->addWidget(new QLabel("Sound: off (enable later with --audio speakers)"));
    side->addSpacing(12);
    side->addWidget(new QLabel(QString("<b>USB sticks</b> (two slots, read-only)")));
    m_stickRows = new QVBoxLayout;
    side->addLayout(m_stickRows);
    side->addStretch();

    auto* root = new QHBoxLayout(this);
    root->addLayout(plate, 1);
    root->addLayout(side);

    connect(m_s3, &S3::ledsChanged, this, qOverload<>(&QWidget::update));
    connect(m_s3, &S3::ledsChanged, this, [this] { for (auto* w : findChildren<QWidget*>()) w->update(); });
    connect(m_s3, &S3::connectedChanged, this, [this](bool c) {
        m_status->setText(c ? "Connected to the Pi's UART." : "Waiting for the Pi…");
    });
    connect(m_machine, &Machine::status, m_status, &QLabel::setText);
    connect(m_sticks, &Sticks::changed, this, &DeckWindow::refreshSticks);
    auto* poll = new QTimer(this); // sticks come and go on the Mac too
    connect(poll, &QTimer::timeout, this, &DeckWindow::refreshSticks);
    poll->start(3000);
    refreshSticks();
    resize(1100, 720);
}

void DeckWindow::refreshSticks() {
    while (QLayoutItem* it = m_stickRows->takeAt(0)) {
        if (it->layout()) {
            while (QLayoutItem* sub = it->layout()->takeAt(0)) { delete sub->widget(); delete sub; }
        }
        delete it->widget();
        delete it;
    }
    const auto sticks = m_sticks->list();
    int inserted = 0;
    for (const auto& s : sticks) inserted += s.inserted;
    if (sticks.isEmpty()) m_stickRows->addWidget(new QLabel("No USB storage on this Mac."));
    for (const auto& s : sticks) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(QString("%1  <small>%2 · %3 GB</small>")
                                      .arg(s.name.isEmpty() ? s.id : s.name, s.id)
                                      .arg(s.size / 1e9, 0, 'f', 1)), 1);
        auto* b = new QPushButton(s.inserted ? "Unplug" : "Insert");
        b->setEnabled(s.inserted || inserted < Sticks::kSlots);
        connect(b, &QPushButton::clicked, this, [this, id = s.id, in = s.inserted, b] {
            b->setEnabled(false);
            auto done = [this](bool ok, const QString& msg) {
                if (!ok) QMessageBox::warning(this, "USB stick", msg);
                QTimer::singleShot(0, this, &DeckWindow::refreshSticks); // not from inside the click
            };
            if (in) m_sticks->unplug(id, done); else m_sticks->insert(id, done);
        });
        row->addWidget(b);
        m_stickRows->addLayout(row);
    }
}
