#pragma once
// Where things are. Two checkouts matter:
//  - the code checkout: the git checkout around the working directory, a
//    worktree or the main one. Its code is what gets deployed and built
//    (pi-qemu/deploy, mixxx_config/units, pi-qemu/release).
//  - the main checkout, which holds what every checkout shares, in two
//    git-ignored folders at its root (cache/cache.h; `pi-qemu cache` lists
//    them): .cache/, what pi-qemu makes again (the built QEMU, the cards, the
//    golden snapshot), and archives/, what is hard to make again (the
//    releases, the decks' identities, the CDJs' firmware, the USB sticks),
//    which no command clears. And the secrets, in pi-qemu/image/secrets.env.
// Outside any checkout, both are the checkout this pi-qemu was built from.
// The emulated decks and CDJs keep their own state in ~/.pi-qemu: short
// paths, for the Unix sockets in it.

#include <QString>

#include <functional>

namespace paths {

QString checkout();      // the code checkout's top
QString mainCheckout();  // the main checkout's top
bool    isWorktree();    // the code checkout is a worktree of the main one

QString piq();           // checkout()/pi-qemu: deploy steps, release and image files
QString units();         // checkout()/mixxx_config/units
QString shared();        // mainCheckout()/pi-qemu
QString cache();         // $PI_QEMU_CACHE, or mainCheckout()/.cache: what pi-qemu makes again
QString archives();      // $PI_QEMU_ARCHIVES, or mainCheckout()/archives: what is hard to make again
QString tools();         // $PI_QEMU_TOOLS, or mainCheckout()/.cache/qemu/bin: qemu-system-aarch64, dtmerge
QString instances();     // $PI_QEMU_INSTANCES, or ~/.pi-qemu/instances (short: socket paths)
QString cdjs();          // $PI_QEMU_CDJ, or ~/.pi-qemu/cdj: each emulated CDJ's own (sockets too)
QString golden();        // cache()/golden/trimixxx0 (.img, .state)
QString releases();      // archives()/releases: every release built, VERSION/
QString identities();    // archives()/decks: each deck's /data, made by `release card`
QString sticks();        // archives()/sticks: USB sticks, as images, folders and .stick files
QString captures();      // archives()/captures: what `deck prepare` copied off each deck
QString secretsFile();   // shared()/image/secrets.env (gitignored)
QString binary();        // this pi-qemu
QString sshKey();        // $SSH_KEY, else ~/.ssh/no_pass/rsa_sam or with_pass/rsa_sam: the key the cards trust

// Called once, the first time cache(), archives() or tools() gives its own
// place rather than its variable's: the command line's check that nothing
// is still where it was before .cache/ and archives/ (cache::requireMoved).
void setLayoutCheck(std::function<void()> check);

} // namespace paths
