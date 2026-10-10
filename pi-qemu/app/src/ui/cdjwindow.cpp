#include "ui/cdjwindow.h"

#include "cdj/panel.h"
#include "cdj/panelclient.h"
#include "util/tool.h"

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>

#include <sys/stat.h>

namespace {

// The deck panel's colours (ui/deckwindow.cpp), and its lamps'.
const QColor kPlate(24, 24, 26), kOff(48, 48, 52), kText(200, 200, 205), kBezel(8, 8, 10);
const QColor kGreen(60, 220, 60), kCueOrange(255, 150, 0), kOrange(255, 160, 0), kRed(240, 60, 60),
    kWhite(235, 235, 240);

// A click is held at least this long, as `cdj press` holds a key: shorter,
// and the firmware may never sample it.
constexpr qint64 kMinHoldMs = 100;
// Pixels of a drag per detent of the browse encoder.
constexpr double kDetentPx = 12;

using Shape = CdjPlace::Shape;
using Lamp = CdjPlace::Lamp;

CdjPlace key(const QString& key, QRectF r, const QString& label, QVector<Lamp> lamps = {}) {
    return {Shape::Key, key, r, label, std::move(lamps)};
}

QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

} // namespace

// The NXS's top panel, schematically: the screen with the browse keys over
// it, the sources down its left and the selector on its right; the hot cues
// and the loop under it; the transport, the jog and the tempo section below.
const QVector<CdjPlace>& cdjPlaces() {
    static const QVector<CdjPlace> places{
        // ---- around the screen
        {Shape::Lid, "SD OPEN", {8, 8, 100, 24}, "SD LID", {{"SD_INDICATOR", kOrange, "SD"}}},
        key("EJECT", {604, 8, 72, 24}, "EJECT", {{"EJECT", kWhite}}),
        key("BROWSE", {118, 36, 114, 28}, "BROWSE", {{"BROWSE", kWhite}}),
        key("TAG LIST", {240, 36, 114, 28}, "TAG LIST", {{"TAG_LIST", kWhite}}),
        key("INFORMATION", {362, 36, 114, 28}, "INFO", {{"INFO", kWhite}}),
        key("MENU", {484, 36, 114, 28}, "MENU / UTILITY", {{"MENU", kWhite}}),
        key("REKORDBOX", {8, 70, 100, 34}, "rekordbox", {{"SOURCE_REKORDBOX", kWhite}}),
        key("LINK", {8, 112, 100, 34}, "LINK", {{"SOURCE_LINK", kWhite}}),
        key("USB", {8, 154, 100, 34}, "USB", {{"SOURCE_USB", kWhite}}),
        key("SD", {8, 196, 100, 34}, "SD", {{"SOURCE_SD", kWhite}}),
        key("DISC", {8, 238, 100, 34}, "DISC", {{"SOURCE_DISC", kWhite}}),
        {Shape::Encoder, "ENCODER PUSH", {618, 70, 92, 92}, "SELECT", {{"ROTARY_SELECTOR", kWhite}}},
        key("RETURN", {716, 70, 76, 30}, "BACK"),
        key("TAG TRACK", {716, 108, 76, 30}, "TAG TRACK"),
        key("TIME/ACUE", {610, 176, 88, 28}, "TIME / A.CUE"),
        key("QUANTIZE", {704, 176, 88, 28}, "QUANTIZE", {{"QUANTIZE", kRed}}),
        key("MEMORY", {610, 212, 88, 28}, "MEMORY"),
        key("DELETE", {704, 212, 88, 28}, "DELETE"),
        key("< CALL", {610, 248, 88, 28}, "< CALL"),
        key("CALL >", {704, 248, 88, 28}, "CALL >"),
        // ---- hot cues and the loop, under the screen
        key("HOT CUE A", {118, 316, 78, 38}, "A",
            {{"HOT_CUE_A_REC", kRed}, {"HOT_CUE_A_LOOP", kOrange}, {"HOT_CUE_A", kGreen}}),
        key("HOT CUE B", {202, 316, 78, 38}, "B",
            {{"HOT_CUE_B_REC", kRed}, {"HOT_CUE_B_LOOP", kOrange}, {"HOT_CUE_B", kGreen}}),
        key("HOT CUE C", {286, 316, 78, 38}, "C",
            {{"HOT_CUE_C_REC", kRed}, {"HOT_CUE_C_LOOP", kOrange}, {"HOT_CUE_C", kGreen}}),
        key("REC MODE", {370, 316, 74, 38}, "REC MODE"),
        key("SLIP", {450, 316, 70, 38}, "SLIP", {{"SLIP", kRed}}),
        key("LOOP MODE", {526, 316, 72, 38}, "LOOP MODE", {{"LOOP_MODE", kOrange}}),
        key("IN", {118, 362, 78, 32}, "LOOP IN", {{"LOOP_IN", kOrange}}),
        key("OUT", {202, 362, 78, 32}, "LOOP OUT", {{"LOOP_OUT", kOrange}}),
        key("RELOOP", {286, 362, 78, 32}, "RELOOP / EXIT", {{"RELOOP_EXIT", kOrange}}),
        // ---- transport, left of the jog
        {Shape::Round, "CUE", {20, 420, 76, 76}, "CUE", {{"CUE", kCueOrange}}},
        {Shape::Round, "PLAY", {20, 512, 76, 76}, "PLAY / PAUSE", {{"PLAY_PAUSE", kGreen}}},
        key("PREVIOUS |<<", {118, 410, 56, 30}, "|<<"),
        key("NEXT >>|", {180, 410, 56, 30}, ">>|"),
        key("REV <<", {118, 446, 56, 30}, "<<"),
        key("FWD >>", {180, 446, 56, 30}, ">>"),
        key("JOG MODE", {118, 488, 118, 30}, "JOG MODE",
            {{"JOG_VINYL", kWhite, "VINYL"}, {"JOG_CDJ", kOrange, "CDJ"}}),
        {Shape::Lever, "REV", {118, 556, 56, 30}, "FWD / REV", {{"REV", kRed}}},
        key("LOCK", {180, 556, 56, 30}, "LOCK"),
        // ---- the jog: only its touch-sensitive top is emulated
        {Shape::Jog, "JOG TOUCH SW", {256, 404, 186, 186}, "JOG TOUCH", {{"JOG_DISPLAY_TOUCH", kWhite}}},
        // ---- tempo and sync, right of the jog
        key("SYNC", {470, 410, 94, 34}, "SYNC", {{"SYNC", kOrange}}),
        key("MASTER", {572, 410, 94, 34}, "MASTER", {{"MASTER", kOrange}}),
        key("TEMPO RANGE", {470, 452, 94, 30}, "TEMPO RANGE"),
        key("MASTER TEMPO", {572, 452, 94, 30}, "MASTER TEMPO", {{"MASTER_TEMPO", kRed}}),
        key("TEMPO RESET", {470, 490, 94, 30}, "TEMPO RESET", {{"TEMPO_RESET", kGreen}}),
    };
    return places;
}

CdjWindow::CdjWindow(const QString& name, const QString& runDir, quint16 port, QWidget* parent)
    : QWidget(parent), m_runDir(runDir), m_panel(new cdj::PanelClient(port, 100, this)) {
    m_clock.start();
    setWindowTitle(QString(kTool) + " — " + name + ": CDJ-2000NXS");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setMinimumSize(int(kCdjPlate.width() * 0.6), int(kCdjPlate.height() * 0.6));
    connect(m_panel, &cdj::PanelClient::lampsChanged, this, [this](const QHash<QString, QString>& lamps) {
        m_lamps = lamps;
        update();
    });
    connect(m_panel, &cdj::PanelClient::connectedChanged, this, [this](bool c) {
        m_connected = c;
        if (!c) m_lamps.clear();
        update();
    });
    // The screen: the GUI board publishes each new frame by a rename, so a
    // frame is read whole or not at all.
    auto* screen = new QTimer(this);
    connect(screen, &QTimer::timeout, this, &CdjWindow::refreshScreen);
    screen->start(33);
    // A blinking lamp blinks here at this rate, whatever MAIN's own.
    auto* blink = new QTimer(this);
    connect(blink, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        update();
    });
    blink->start(300);
    // Keys clicked shorter than a press go up once they have lasted one.
    auto* release = new QTimer(this);
    connect(release, &QTimer::timeout, this, [this] {
        for (auto it = m_held.begin(); it != m_held.end();) {
            const QString k = it.key();
            if (it.value() < 0 && m_clock.elapsed() >= -it.value()) {
                if (const cdj::Key* kk = cdj::key(k)) m_panel->send(cdj::upLine(*kk));
                it = m_held.erase(it);
                update();
            } else {
                ++it;
            }
        }
    });
    release->start(10);
    refreshScreen();
}

CdjWindow::~CdjWindow() {
    releaseAll(); // closed or not: a key left down would stay down on the CDJ
    // The channel goes first, while what its signals reach is still here:
    // QWidget would delete it only once this window's members are gone.
    delete m_panel;
}

QSize CdjWindow::sizeHint() const {
    // As big as fits: the screen at twice its pixels if there is room.
    double scale = 2.0;
    if (const QScreen* s = screen()) {
        const QSize room = s->availableSize() * 0.9;
        scale = qMin(scale, qMin(room.width() / kCdjPlate.width(), room.height() / kCdjPlate.height()));
    }
    return (kCdjPlate.size() * qMax(scale, 0.6)).toSize();
}

QTransform CdjWindow::toWidget() const {
    const double s = qMin(width() / kCdjPlate.width(), height() / kCdjPlate.height());
    const double dx = (width() - kCdjPlate.width() * s) / 2, dy = (height() - kCdjPlate.height() * s) / 2;
    return QTransform(s, 0, 0, s, dx, dy);
}

const CdjPlace* CdjWindow::placeAt(QPointF pos) const {
    const QPointF p = toWidget().inverted().map(pos);
    for (const CdjPlace& place : cdjPlaces()) {
        const QRectF r = place.rect;
        const bool round = place.shape == Shape::Round || place.shape == Shape::Encoder || place.shape == Shape::Jog;
        if (round ? QLineF(r.center(), p).length() <= r.width() / 2 : r.contains(p)) return &place;
    }
    return nullptr;
}

void CdjWindow::refreshScreen() {
    const QString path = m_runDir + "/screen.ppm";
    struct stat st {};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return;
#ifdef __APPLE__
    const timespec& mtime = st.st_mtimespec;
#else
    const timespec& mtime = st.st_mtim;
#endif
    const qint64 stamp = qint64(mtime.tv_sec) * 1000000000 + mtime.tv_nsec + st.st_size + qint64(st.st_ino);
    if (stamp == m_screenStamp) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray ppm = f.readAll();
    // P6, 480x255 (the PPI's capture: the panel is its top 234 lines), 255.
    QImage image;
    if (!image.loadFromData(ppm, "PPM")) return;
    m_screen = image.copy(0, 0, int(kCdjScreen.width()), qMin(image.height(), int(kCdjScreen.height())));
    m_screenStamp = stamp;
    update();
}

void CdjWindow::hold(const QString& name) {
    const cdj::Key* k = cdj::key(name);
    if (!k || m_held.contains(name)) return;
    m_panel->send(cdj::downLine(*k));
    m_held.insert(name, m_clock.elapsed());
    update();
}

void CdjWindow::release(const QString& name) {
    const cdj::Key* k = cdj::key(name);
    if (!k || !m_held.contains(name)) return;
    const qint64 since = m_held.value(name);
    if (since < 0) return; // already going up
    if (m_clock.elapsed() - since < kMinHoldMs) {
        m_held[name] = -(since + kMinHoldMs); // up then
        return;
    }
    m_panel->send(cdj::upLine(*k));
    m_held.remove(name);
    update();
}

void CdjWindow::releaseAll() {
    for (const QString& name : m_held.keys())
        if (const cdj::Key* k = cdj::key(name)) m_panel->send(cdj::upLine(*k));
    m_held.clear();
    m_latched.clear();
}

void CdjWindow::mousePressEvent(QMouseEvent* e) {
    const CdjPlace* place = placeAt(e->position());
    if (!place || e->button() != Qt::LeftButton) return;
    m_mouseKey = place->key;
    switch (place->shape) {
    case Shape::Lever:
        // The DIRECTION lever is a switch, active low: REV is the contact open.
        m_rev = !m_rev;
        m_panel->send(QString("level 15 02 %1").arg(m_rev ? 0 : 1));
        update();
        return;
    case Shape::Lid:
        m_lidOpen = !m_lidOpen;
        m_panel->send(m_lidOpen ? "sd-lid open" : "sd-lid closed");
        update();
        return;
    case Shape::Encoder:
        m_dragY = e->position().y();
        m_turned = false;
        return;
    default:
        break;
    }
    // Shift-click latches a key down, for a chord or a long hold.
    if (e->modifiers() & Qt::ShiftModifier) {
        if (m_latched.remove(place->key)) {
            release(place->key);
        } else {
            m_latched.insert(place->key);
            hold(place->key);
        }
        m_mouseKey.clear();
        return;
    }
    hold(place->key);
}

void CdjWindow::mouseMoveEvent(QMouseEvent* e) {
    if (m_mouseKey != "ENCODER PUSH") return;
    // Down the screen is down the list, as the knob turned clockwise.
    const double scale = toWidget().m11();
    const int detents = int((e->position().y() - m_dragY) / (kDetentPx * scale / 2));
    if (!detents) return;
    m_dragY += detents * kDetentPx * scale / 2;
    m_turned = true;
    m_panel->send(cdj::rotaryLine(detents));
}

void CdjWindow::mouseReleaseEvent(QMouseEvent*) {
    const QString name = m_mouseKey;
    m_mouseKey.clear();
    if (name.isEmpty() || m_latched.contains(name)) return;
    if (name == "ENCODER PUSH") {
        if (!m_turned) {
            hold(name); // a click on the knob that did not turn it is its push
            release(name);
        }
        return;
    }
    const CdjPlace* place = nullptr;
    for (const CdjPlace& p : cdjPlaces())
        if (p.key == name) place = &p;
    if (place && (place->shape == Shape::Lever || place->shape == Shape::Lid)) return;
    release(name);
}

void CdjWindow::wheelEvent(QWheelEvent* e) {
    const CdjPlace* place = placeAt(e->position());
    if (!place || place->shape != Shape::Encoder) return;
    // A notch of a wheel is 120; a trackpad sends it in small steps.
    m_wheel -= e->angleDelta().y();
    const int detents = m_wheel / 120;
    m_wheel -= detents * 120;
    if (detents) m_panel->send(cdj::rotaryLine(detents));
}

// A few keys from the keyboard: space PLAY, C CUE, up and down the selector,
// return its push, backspace or escape BACK.
static QString keyFor(int key) {
    switch (key) {
    case Qt::Key_Space: return "PLAY";
    case Qt::Key_C: return "CUE";
    case Qt::Key_Return:
    case Qt::Key_Enter: return "ENCODER PUSH";
    case Qt::Key_Backspace:
    case Qt::Key_Escape: return "RETURN";
    default: return {};
    }
}

void CdjWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
        m_panel->send(cdj::rotaryLine(e->key() == Qt::Key_Down ? 1 : -1));
        return;
    }
    const QString name = keyFor(e->key());
    if (name.isEmpty()) return QWidget::keyPressEvent(e);
    if (!e->isAutoRepeat()) hold(name);
}

void CdjWindow::keyReleaseEvent(QKeyEvent* e) {
    const QString name = keyFor(e->key());
    if (name.isEmpty()) return QWidget::keyReleaseEvent(e);
    if (!e->isAutoRepeat()) release(name);
}

void CdjWindow::closeEvent(QCloseEvent* e) {
    // What the window holds down, it lets go: the CDJ runs on without it.
    releaseAll();
    QWidget::closeEvent(e);
}

void CdjWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPlate);
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(toWidget());

    // The screen, in its bezel; its pixels as they are when the scale allows.
    p.fillRect(kCdjScreen.adjusted(-6, -6, 6, 6), kBezel);
    if (!m_screen.isNull()) {
        const double s = p.transform().m11();
        p.setRenderHint(QPainter::SmoothPixmapTransform, qAbs(s - qRound(s)) > 0.01);
        p.drawImage(kCdjScreen, m_screen);
    } else {
        p.setPen(kText);
        p.drawText(kCdjScreen, Qt::AlignCenter, "no picture from the CDJ yet");
    }
    // The disc slot over it.
    p.fillRect(QRectF(118, 12, 470, 14), kBezel);
    p.setPen(QColor(110, 110, 115));
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.drawText(QRectF(118, 12, 470, 14), Qt::AlignCenter, "DISC");

    auto shown = [this](const CdjPlace& place) -> QColor { // the key's light, or kOff
        for (const Lamp& lamp : place.lamps) {
            if (!lamp.caption.isEmpty()) continue;
            switch (cdj::lit(m_lamps, lamp.name)) {
            case cdj::Lit::On: return lamp.colour;
            case cdj::Lit::Dim: return mix(kOff, lamp.colour, 0.3);
            case cdj::Lit::Blink: // a two-bit lamp blinks between its dim and lit levels
                return m_blinkOn ? lamp.colour : cdj::twoBit(lamp.name) ? mix(kOff, lamp.colour, 0.3) : kOff;
            case cdj::Lit::Off: break;
            }
        }
        return kOff;
    };

    for (const CdjPlace& place : cdjPlaces()) {
        const QColor fill = shown(place);
        const bool held = m_held.contains(place.key) || m_latched.contains(place.key) ||
                          (place.shape == Shape::Lever && m_rev) || (place.shape == Shape::Lid && m_lidOpen);
        const QPen edge(held ? Qt::white : QColor(90, 90, 95), held ? 2.0 : 1.0);
        const QRectF r = place.rect;
        f.setPixelSize(r.height() > 40 ? 12 : 10);
        f.setBold(true);
        p.setFont(f);
        const QColor ink = fill.lightnessF() > 0.55 ? QColor(20, 20, 22) : kText;
        switch (place.shape) {
        case Shape::Key:
        case Shape::Lever:
        case Shape::Lid: {
            p.setPen(edge);
            p.setBrush(fill);
            p.drawRoundedRect(r, 4, 4);
            p.setPen(ink);
            QString label = place.label;
            if (place.shape == Shape::Lever) label = m_rev ? "REV" : "FWD";
            if (place.shape == Shape::Lid) label = m_lidOpen ? "SD LID: OPEN" : "SD LID";
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, label);
            break;
        }
        case Shape::Round:
            p.setPen(edge);
            p.setBrush(fill);
            p.drawEllipse(r);
            p.setPen(ink);
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, place.label);
            break;
        case Shape::Encoder: {
            p.setPen(QPen(fill == kOff ? QColor(70, 70, 75) : fill, 4));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r);
            p.setPen(QPen(QColor(120, 120, 125), held ? 2.5 : 1.5));
            p.setBrush(held ? QColor(90, 90, 100) : QColor(40, 40, 44));
            p.drawEllipse(r.adjusted(8, 8, -8, -8));
            p.setPen(kText);
            p.drawText(r, Qt::AlignCenter, "SELECT\npush / drag");
            break;
        }
        case Shape::Jog: {
            p.setPen(QPen(QColor(110, 110, 115), 3));
            p.setBrush(QColor(36, 36, 40));
            p.drawEllipse(r);
            const QRectF top = r.adjusted(r.width() * 0.16, r.height() * 0.16, -r.width() * 0.16, -r.height() * 0.16);
            p.setPen(QPen(fill == kOff ? QColor(70, 70, 75) : fill, 3));
            p.setBrush(held ? QColor(70, 70, 80) : QColor(20, 20, 22));
            p.drawEllipse(top);
            p.setPen(kText);
            p.drawText(top, Qt::AlignCenter, "JOG\n(touch only)");
            break;
        }
        }
        // Small lamps beside a key: JOG MODE's VINYL and CDJ, the SD indicator.
        double x = r.left();
        for (const Lamp& lamp : place.lamps) {
            if (lamp.caption.isEmpty()) continue;
            const cdj::Lit l = cdj::lit(m_lamps, lamp.name);
            const bool on = l == cdj::Lit::On || l == cdj::Lit::Dim || (l == cdj::Lit::Blink && m_blinkOn);
            const QRectF dot(x, r.bottom() + 4, 8, 8);
            p.setPen(Qt::NoPen);
            p.setBrush(on ? (l == cdj::Lit::Dim ? mix(kOff, lamp.colour, 0.3) : lamp.colour) : kOff);
            p.drawEllipse(dot);
            f.setPixelSize(9);
            f.setBold(false);
            p.setFont(f);
            p.setPen(kText);
            p.drawText(QRectF(x + 11, r.bottom() + 1, 50, 14), Qt::AlignLeft | Qt::AlignVCenter, lamp.caption);
            x += 58;
        }
    }

    // What the window is showing, along the bottom.
    f.setPixelSize(10);
    f.setBold(false);
    p.setFont(f);
    p.setPen(m_connected ? QColor(130, 200, 130) : QColor(220, 180, 60));
    p.drawText(QRectF(470, 540, 322, 54), Qt::AlignRight | Qt::AlignBottom | Qt::TextWordWrap,
               m_connected ? "lamps: as the CDJ's firmware lights them\nclick holds a key; shift-click latches it"
                           : "waiting for the CDJ's control channel…");
}
