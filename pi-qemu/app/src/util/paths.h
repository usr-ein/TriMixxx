#pragma once
// Where things are. Two checkouts matter:
//  - the code checkout: the git checkout around the working directory, a
//    worktree or the main one. Its code is what gets deployed and built
//    (pi-qemu/deploy, mixxx_config/units, pi-qemu/release).
//  - the main checkout, which holds what every checkout shares: the built
//    QEMU, the card caches, the golden snapshot, the secrets.
// Outside any checkout, both are the checkout this pi-qemu was built from.

#include <QString>

namespace paths {

QString checkout();      // the code checkout's top
QString mainCheckout();  // the main checkout's top
bool    isWorktree();    // the code checkout is a worktree of the main one

QString piq();           // checkout()/pi-qemu: deploy steps, release and image files
QString units();         // checkout()/mixxx_config/units
QString shared();        // mainCheckout()/pi-qemu
QString cache();         // $PI_QEMU_CACHE, or shared()/.cache: cards, the golden pair, identities
QString tools();         // shared()/qemu/.build/bin: qemu-system-aarch64, dtmerge
QString instances();     // $PI_QEMU_INSTANCES, or ~/.pi-qemu/instances (short: socket paths)
QString golden();        // cache()/golden/trimixxx0 (.img, .state)
QString releases();      // shared()/release/out: every release built, out/VERSION/
QString identities();    // cache()/decks: each deck's /data, made by `release card`
QString secretsFile();   // shared()/image/secrets.env (gitignored)
QString binary();        // this pi-qemu
QString sshKey();        // $SSH_KEY, else ~/.ssh/no_pass/rsa_sam or with_pass/rsa_sam: the key the cards trust

} // namespace paths
