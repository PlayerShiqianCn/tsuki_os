#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP="$ROOT/build/host-tests"
CC=${CC:-gcc}

mkdir -p "$TMP"

"$CC" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -I"$ROOT/include" \
    "$ROOT/tests/core_regression.c" "$ROOT/kernel/core.c" \
    -o "$TMP/core_regression"

"$CC" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -I"$ROOT/include" \
    "$ROOT/tests/jpeg_regression.c" "$ROOT/userspace/jpeg.c" \
    -o "$TMP/jpeg_regression"

# Host tests execute the real filesystem implementation, replacing only the
# privileged interrupt save/restore instructions in a generated test copy.
sed \
    -e 's/__asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) :: "memory");/flags = 0;/' \
    -e 's/__asm__ volatile ("push %0; popf" :: "r"(flags) : "memory", "cc");/(void)flags;/' \
    "$ROOT/fs/fs.c" > "$TMP/fs_host.c"

"$CC" -std=c11 -Wall -Wextra -Werror -Wno-int-to-pointer-cast \
    -Wno-pointer-to-int-cast -g -fno-builtin \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -I"$ROOT/include" \
    "$ROOT/tests/fs_regression.c" "$ROOT/tests/fs_disk_shim.c" "$TMP/fs_host.c" \
    -o "$TMP/fs_regression"

run_fs() {
    name=$1
    image="$TMP/$name.img"
    cp "$ROOT/build/os-image.img" "$image"
    "$TMP/fs_regression" "$name" "$image"
}

case "${1:-all}" in
    baseline)
        "$TMP/jpeg_regression" valid "$ROOT/tests/fixtures/baseline.jpg"
        ;;
    valid|invalid-al|huge)
        "$TMP/jpeg_regression" "$1" "$ROOT/tsk_girl.jpg"
        ;;
    bounded|shrink|truncate|alloc-fail|bad-dir)
        run_fs "$1"
        ;;
    all)
        "$TMP/core_regression"
        "$TMP/jpeg_regression" valid "$ROOT/tsk_girl.jpg"
        "$TMP/jpeg_regression" valid "$ROOT/tests/fixtures/baseline.jpg"
        "$TMP/jpeg_regression" invalid-al "$ROOT/tsk_girl.jpg"
        "$TMP/jpeg_regression" huge "$ROOT/tsk_girl.jpg"
        run_fs bounded
        run_fs shrink
        run_fs truncate
        run_fs alloc-fail
        run_fs bad-dir
        ;;
    *)
        echo "unknown test: $1" >&2
        exit 2
        ;;
esac
