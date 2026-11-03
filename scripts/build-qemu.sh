#!/bin/bash
# license:BSD-3-Clause
#
# Build QEMU (../qemu, next to this MAME tree) as one static archive for the MaNE binary.
#
# Configures QEMU out of tree in build/qemu, builds qemu-system-arm, and
# turns its link command into:
#   build/qemu/libqemu-mane.a    every object of qemu-system-arm except
#                                system/main.c; linked force-loaded, so the
#                                linker keeps QEMU's constructor-registered
#                                machines and devices
#   build/qemu/libqemu-mane-deps.a  the static libraries from the same link
#                                (libqemuutil.a, libfdt.a), linked normally;
#                                libqemuutil.a holds QEMU's stubs, which
#                                clash with the real code if force-loaded
#   build/qemu/link-flags.txt    the -L, -l, -framework, rpath and shared
#                                library arguments from the same link
#   build/qemu/stamp             the submodule commit these were built from
#
# Does nothing when the stamp matches the submodule commit and the outputs
# exist. Links nothing itself. bash 3.2 and BSD tools.

set -e
set -f

MAME_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC=$MAME_DIR/../qemu
OUT=$MAME_DIR/build/qemu
LIB=$OUT/libqemu-mane.a
DEPS=$OUT/libqemu-mane-deps.a
FLAGS=$OUT/link-flags.txt
STAMP=$OUT/stamp

if [ ! -f "$SRC/configure" ]; then
	echo "build-qemu: $SRC is empty; run: git submodule update --init qemu (in the pinesy repo)" >&2
	exit 1
fi

COMMIT=$(git -C "$SRC" rev-parse HEAD)
if [ -f "$STAMP" ] && [ -f "$LIB" ] && [ -f "$DEPS" ] && [ -f "$FLAGS" ] &&
   [ "$(cat "$STAMP")" = "$COMMIT" ]; then
	exit 0
fi

echo "build-qemu: building QEMU $COMMIT in $OUT"
mkdir -p "$OUT"
cd "$OUT"

if [ ! -f build.ninja ]; then
	"$SRC/configure" \
		--target-list=arm-softmmu \
		--without-default-features \
		--disable-docs \
		--disable-tools \
		--disable-guest-agent
fi

# On macOS the executable is linked as qemu-system-arm-unsigned and then
# signed into qemu-system-arm.
TARGET=qemu-system-arm
if ninja -t targets all | grep -q '^qemu-system-arm-unsigned:'; then
	TARGET=qemu-system-arm-unsigned
fi

ninja qemu-system-arm

LINE=$(ninja -t commands "$TARGET" | tail -n 1)

OBJS=()
LIBS=()
FLAGLIST=()
first=1
skip_next=0
framework_next=0
for t in $LINE; do
	if [ $first = 1 ]; then first=0; continue; fi      # the compiler driver
	if [ $skip_next = 1 ]; then skip_next=0; continue; fi
	if [ $framework_next = 1 ]; then
		FLAGLIST+=("-framework $t"); framework_next=0; continue
	fi
	case "$t" in
		-o) skip_next=1 ;;
		*system_main.c.o) ;;                          # QEMU's main()
		*.o) OBJS+=("$t") ;;
		*.a) LIBS+=("$t") ;;
		-framework) framework_next=1 ;;
		-l*|-L*|-Wl,-rpath,*|-pthread) FLAGLIST+=("$t") ;;
		*.dylib|*.so|*.so.*) FLAGLIST+=("$t") ;;
		*) ;;                                         # options for QEMU's own executable
	esac
done

if [ ${#OBJS[@]} = 0 ]; then
	echo "build-qemu: no objects found in the link command of $TARGET" >&2
	exit 1
fi

rm -f "$LIB" "$DEPS"
case "$(uname -s)" in
	Darwin)
		libtool -static -no_warning_for_no_symbols -o "$LIB" "${OBJS[@]}"
		libtool -static -no_warning_for_no_symbols -o "$DEPS" "${LIBS[@]}"
		;;
	*)
		{
			echo "CREATE $LIB"
			for o in "${OBJS[@]}"; do echo "ADDMOD $o"; done
			echo "SAVE"
			echo "END"
		} | ar -M
		{
			echo "CREATE $DEPS"
			for l in "${LIBS[@]}"; do echo "ADDLIB $l"; done
			echo "SAVE"
			echo "END"
		} | ar -M
		;;
esac

: > "$FLAGS"
for f in "${FLAGLIST[@]}"; do echo "$f" >> "$FLAGS"; done

echo "$COMMIT" > "$STAMP"
echo "build-qemu: wrote $LIB (${#OBJS[@]} objects), $DEPS (${#LIBS[@]} libraries), $FLAGS"
