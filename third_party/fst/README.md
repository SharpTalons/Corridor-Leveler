# FST (vendored)

`fst/fst.h` from [FST](https://git.iem.at/zmoelnig/FST), "Free Studio Technologies", by IOhannes m zmölnig
(IEM): an independent reimplementation of the 2.x plugin headers. It is licensed under the GNU GPL v3 or
later (see `LICENSE`).

Copied unmodified from commit `647af068765b75867e3a28b4dd8991ab9ed47f7c` (22 April 2025), the commit the
build used to download. It is kept here so the build no longer depends on git.iem.at being reachable from
the CI machines. To use another FST checkout instead, configure with `-DCORRIDOR_FST_DIR=<path>`.
