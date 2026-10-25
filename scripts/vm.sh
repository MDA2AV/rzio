#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Run rzio in a throwaway VM that boots the host's own kernel (virtme-ng).
# The VM sees this directory read-only and the rest of the host filesystem
# through an overlay that vanishes on exit. A kernel crash only takes down
# the VM.
#
#   scripts/vm.sh test    build, load the module, run tools/rzio_test, unload,
#                         and fail on any kernel warning or new taint flag
#   scripts/vm.sh shell   root shell in the VM, in this directory
#
# Environment:
#   RZIO_VM_PIN    host CPUs to run QEMU on, e.g. 16-31 (default: no pinning)
#   RZIO_VM_CPUS   guest CPUs (default 4)
#   RZIO_VM_MEM    guest memory (default 2G)
set -euo pipefail
cd "$(dirname "$0")/.."

# Loading an out-of-tree (O) unsigned (E) module sets these taint flags by
# design. Any other new flag means the kernel hit a problem.
EXPECTED_TAINT=$(( (1 << 12) | (1 << 13) ))

vm() {
	# --rodir: the guest gets a fresh /tmp, so a checkout there would vanish.
	local cmd=(vng --run --user root --cpus "${RZIO_VM_CPUS:-4}"
		   --memory "${RZIO_VM_MEM:-2G}" --rodir="$PWD" "$@")

	if [[ -n "${RZIO_VM_PIN:-}" ]]; then
		cmd=(taskset -c "$RZIO_VM_PIN" "${cmd[@]}")
	fi
	"${cmd[@]}"
}

# Runs inside the VM.
guest_test() {
	local taint_before taint_new status=0

	taint_before=$(</proc/sys/kernel/tainted)
	dmesg --clear

	insmod module/rzio.ko dyndbg=+p || status=1
	if (( status == 0 )); then
		tools/rzio_test || status=1
		rmmod rzio || status=1
		if [[ -e /dev/rzio ]]; then
			echo "FAIL: /dev/rzio still exists after rmmod"
			status=1
		fi
	fi

	echo "--- kernel log"
	dmesg
	if dmesg | grep -qE 'BUG:|WARNING:|Oops|Call Trace|KASAN|UBSAN|KFENCE'; then
		echo "FAIL: the kernel reported a problem"
		status=1
	fi
	taint_new=$(( $(</proc/sys/kernel/tainted) & ~taint_before & ~EXPECTED_TAINT ))
	if (( taint_new )); then
		echo "FAIL: new kernel taint flags: $taint_new"
		status=1
	fi

	(( status == 0 )) && echo "PASS"
	return "$status"
}

case "${1:-test}" in
test)
	make -s
	vm --exec "scripts/vm.sh guest-test"
	;;
shell)
	make -s
	vm
	;;
guest-test)
	guest_test
	;;
*)
	echo "usage: $0 [test|shell]" >&2
	exit 2
	;;
esac
