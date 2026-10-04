#include "deckwindow.h"

#include "MidiMap.hpp"
#include "machine.h"
#include "s3.h"
#include "sticks.h"
#include "wiring.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QTabWidget>
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
        QFont f = font();
        f.setPointSizeF(8);
        setFixedSize(qMax(diameter + 30, QFontMetrics(f).horizontalAdvance(label) + 8), diameter + 18);
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
    explicit Encoder(QWidget* parent) : QWidget(parent) { setFixedSize(70, 86); setCursor(Qt::SizeVerCursor); }

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
        p.drawText(QRect(0, 58, width(), 28), Qt::AlignCenter, "BROWSE\ndrag / click");
    }
    // Drag up or down to turn it, a detent every kStep pixels (up is +); a
    // click that did not turn it is the push.
    void mousePressEvent(QMouseEvent* e) override { m_lastY = e->position().y(); m_turned = false; }
    void mouseMoveEvent(QMouseEvent* e) override {
        const int detents = int((m_lastY - e->position().y()) / kStep);
        if (!detents) return;
        m_lastY -= detents * kStep;
        m_turned = true;
        if (onTurn) onTurn(detents);
        m_pos += detents * 0.4;
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (m_turned || !onPush) return;
        m_pushed = true;
        onPush(true);
        update();
        QTimer::singleShot(80, this, [this] { m_pushed = false; onPush(false); update(); });
    }

private:
    static constexpr double kStep = 12;
    double m_lastY = 0;
    bool m_turned = false;
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

    // A ring pad by what it does, wherever this deck's wiring put it.
    QSet<int> placed; // physical notes the plate has a place for
    auto pad = [&](const QString& control) {
        const int phys = *m_wiring->note(control);
        placed << phys;
        return phys >= midimap::PAD_B_BASE ? padButton(1, phys - midimap::PAD_B_BASE)
                                           : padButton(0, phys - midimap::PAD_A_BASE);
    };
    const DeckLeds* L = &m_s3->leds();

    // The top row: the loop (IN OUT RELOOP) at the start, the loop lengths at the end.
    auto* loops = new QHBoxLayout;
    loops->addWidget(lamp("IN", "loop-in", 30, [L] { return L->loopIn; }, QColor(255, 160, 0)));
    loops->addWidget(lamp("OUT", "loop-out", 30, [L] { return L->loopOut; }, QColor(255, 160, 0)));
    loops->addWidget(lamp("RELOOP", "reloop", 30, {}, {}));
    loops->addStretch();
    for (const char* c : {"loop-double", "loop-halve", "loop4", "loop8"}) loops->addWidget(pad(c));
    // SLIP and SORT over the hot cues, which sit two by two beside the platter.
    auto* slipSort = new QHBoxLayout;
    slipSort->addWidget(pad("slip"));
    slipSort->addWidget(pad("sort"));
    auto* cues = new QGridLayout;
    cues->addWidget(pad("hotcue1"), 0, 0);
    cues->addWidget(pad("hotcue2"), 0, 1);
    cues->addWidget(pad("hotcue3"), 1, 0);
    cues->addWidget(pad("hotcue4"), 1, 1);

    // As wide as it is drawn: a wider widget only puts air around the platter.
    auto* jog = new JogWheel(this);
    jog->setFixedSize(340, 340);
    jog->onTicks = [this](int t) { m_s3->jog(m_wiring->jogReversed() ? -t : t); };
    jog->onTouch = pressFor("jog-touch");

    auto* enc = new Encoder(this);
    enc->onTurn = [this](int d) { m_s3->encoder(d); };
    enc->onPush = pressFor("push");

    // The 14-bit tempo fader; "centre" puts it back on the centre detent.
    auto* fader = new QSlider(Qt::Vertical, this);
    fader->setRange(0, 16383);
    fader->setValue(8192);
    fader->setMinimumHeight(170);
    connect(fader, &QSlider::valueChanged, this, [this](int v) { m_s3->tempo(v); });
    auto* center = new QPushButton("centre");
    connect(center, &QPushButton::clicked, fader, [fader] { fader->setValue(8192); });

    // Right: BACK over the browse encoder, MASTER TEMPO and KEYLOCK over the fader.
    auto* browse = new QHBoxLayout;
    browse->addWidget(pad("back"), 0, Qt::AlignTop);
    browse->addWidget(enc, 0, Qt::AlignTop);
    auto* right = new QVBoxLayout;
    right->addLayout(browse);
    right->addSpacing(8);
    auto* tempoPads = new QHBoxLayout;
    tempoPads->addWidget(pad("tempo-range"));
    tempoPads->addWidget(pad("keylock"));
    right->addLayout(tempoPads);
    right->addWidget(new QLabel("TEMPO"), 0, Qt::AlignHCenter);
    right->addWidget(fader, 1, Qt::AlignHCenter);
    right->addWidget(center);

    // Pads this deck has but nothing is wired to (trimixxx2's eighth), still
    // pressable: on the end of the SLIP SORT row.
    auto* spare = slipSort;
    for (int ring = 0; ring < 2; ring++)
        for (int i = 0; i < m_wiring->ringSize(ring); i++)
            if (!placed.contains((ring == 0 ? midimap::PAD_A_BASE : midimap::PAD_B_BASE) + i))
                spare->addWidget(padButton(ring, i));
    spare->addStretch();
    // CUE over PLAY, under the hot cues, as on the deck.
    auto* transport = new QHBoxLayout;
    transport->addWidget(lamp("CUE", "cue", 66, [L] { return L->cue; }, QColor(255, 150, 0)));
    transport->addWidget(lamp("PLAY / PAUSE", "play", 66, [L] { return L->play; }, QColor(60, 220, 60)));
    transport->addStretch();
    auto* left = new QVBoxLayout;
    left->addStretch();
    left->addLayout(slipSort);
    left->addSpacing(8);
    left->addLayout(cues);
    left->addLayout(transport);
    left->addStretch();


    // The plate as a grid: the loop row across the top; SLIP SORT and the hot
    // cues and the transport, the platter, and browse and tempo on one row.
    auto* controls = new QWidget;
    auto* plate = new QGridLayout(controls);
    plate->addLayout(loops, 0, 0, 1, 3);
    plate->setRowMinimumHeight(1, 4);
    plate->addLayout(left, 2, 0);
    plate->addWidget(jog, 2, 1, Qt::AlignCenter);
    plate->addLayout(right, 2, 2);
    plate->setColumnStretch(1, 1);

    // The other tab: the S3's status LED, the board, the USB sticks.
    auto* board = new QWidget;
    auto* side = new QVBoxLayout(board);
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
    const QString audio = m->options().audio;
    side->addWidget(new QLabel(audio == "none" ? "Sound: off (enable with --audio speakers)"
                               : audio == "speakers" ? "Sound: ON, through the Mac's speakers"
                                                     : "Sound: recording to " + audio.mid(4)));
    side->addSpacing(12);
    side->addWidget(new QLabel(QString("<b>USB sticks</b> (two slots, read-only)")));
    m_stickRows = new QVBoxLayout;
    side->addLayout(m_stickRows);
    side->addStretch();

    for (QWidget* page : {controls, board}) { // the plate's colour, not the tab pane's
        page->setPalette(pal);
        page->setAutoFillBackground(true);
    }
    auto* tabs = new QTabWidget(this);
    tabs->addTab(controls, "Controls");
    tabs->addTab(board, "S3, power, USB sticks");
    auto* root = new QHBoxLayout(this);
    root->addWidget(tabs);

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
    adjustSize();
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
