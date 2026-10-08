#include "decks/emulateddeck.h"

#include "decks/controlclient.h"
#include "util/fail.h"

#include <QFileInfo>

QString EmulatedDeck::controls(const QStringList& words) {
    requireUp();
    const control::Reply r = control::send(m_instance.controlSocket(), words.join(' '));
    if (!r.ok) fail(r.text);
    return r.text;
}

void EmulatedDeck::shot(const QString& png) {
    // QEMU's own screendump: the Pi's framebuffer, window or not.
    controls({"screenshot", QFileInfo(png).absoluteFilePath()});
}
