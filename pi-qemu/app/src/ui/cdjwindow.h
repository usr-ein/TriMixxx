#pragma once
// An emulated CDJ-2000NXS in a window (`pi-qemu cdj window NAME`): its
// screen, live, in a schematic of its top panel in the deck panel's look.
// Every key `cdj press` knows is a pad in its place; a click holds it on the
// emulator's control channel as long as the mouse is down (at least as long
// as a `press`). The pads light as the CDJ's own firmware lights its lamps:
// what MAIN last sent the panel, as the channel's `state` names it. The jog
// is not emulated: only its touch-sensitive top is here.

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QVector>
#include <QWidget>

namespace cdj {
class PanelClient;
}

// Where a control of the NXS sits on the plate. One unit is one pixel of the
// CDJ's 480x234 screen, so the screen is never resampled at a whole scale.
struct CdjPlace {
    enum class Shape { Key, Round, Encoder, Jog, Lever, Lid };
    struct Lamp {
        QString name;    // as the emulator names it: "PLAY_PAUSE"
        QColor  colour;
        QString caption; // a small lamp beside the key, with this caption
        Lamp(QString n, QColor c, QString cap = {}) : name(std::move(n)), colour(c), caption(std::move(cap)) {}
    };
    Shape   shape = Shape::Key;
    QString key; // cdj::Key's name
    QRectF  rect;
    QString label;
    QVector<Lamp> lamps; // the key's own light: the first one lit shows
};

const QVector<CdjPlace>& cdjPlaces();
inline constexpr QRectF kCdjPlate{0, 0, 800, 600};
inline constexpr QRectF kCdjScreen{118, 70, 480, 234};

class CdjWindow : public QWidget {
    Q_OBJECT
public:
    // The CDJ's run directory (its screen.ppm) and its control channel's port.
    CdjWindow(const QString& name, const QString& runDir, quint16 port, QWidget* parent = nullptr);
    ~CdjWindow() override;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    QTransform toWidget() const; // plate units -> widget pixels
    const CdjPlace* placeAt(QPointF widgetPos) const;
    void hold(const QString& key);
    void release(const QString& key);
    void releaseAll();
    void refreshScreen();

    QString m_runDir;
    cdj::PanelClient* m_panel;
    QImage  m_screen;
    qint64  m_screenStamp = 0; // the published frame's mtime and size, to spot a new one
    QHash<QString, QString> m_lamps;
    bool    m_connected = false;
    bool    m_blinkOn = true;
    // Keys held down on the channel, and since when (a click lasts at least
    // as long as a `press`, or the firmware may never see it).
    QHash<QString, qint64> m_held;
    QSet<QString> m_latched; // shift-click: down until clicked again
    QString m_mouseKey;      // the key under a press of the mouse
    double  m_dragY = 0;
    int     m_wheel = 0;     // a wheel's turn not yet a whole detent
    bool    m_turned = false;
    bool    m_rev = false;   // the DIRECTION lever (the window's; it starts at FWD)
    bool    m_lidOpen = false;
    QElapsedTimer m_clock;
};
