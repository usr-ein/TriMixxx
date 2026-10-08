#pragma once
// Waiting for a deck: until ssh answers, and until Mixxx is ready for the
// S3. `alive` runs between tries, and fails when waiting has become pointless
// (an emulated deck's pi-qemu exited).

#include "util/sshtarget.h"

#include <functional>

namespace readiness {

void waitSsh(const SshTarget& t, int seconds, const std::function<void()>& alive = {});

enum class Mixxx { Ready, NotYet, Deaf };
// Mixxx runs, holding its own /tmp/mixxx/mixxx.log open (not the previous
// run's, which it renames as it starts), and that log says the sound stream
// started and the deck's controller (the S3's MIDI) is open; the skin is
// loaded before either. And the MIDI bridge (ttymidi) is older than this
// Mixxx: Mixxx opens the bridge's port once, at start, so a bridge restarted
// since has left it deaf to the S3.
Mixxx mixxx(const SshTarget& t);

// Until Mixxx is ready; fails after `seconds`, or at once if it is deaf.
// `target` is how the person names the deck, for the advice: "NAME" or
// "--host ALIAS".
void waitMixxx(const SshTarget& t, int seconds, const QString& target, const std::function<void()>& alive = {});

} // namespace readiness
