# rzio

Socket I/O rings for web servers, running in the Linux kernel (ring 0).

rzio is a learning project. It builds an io_uring-style interface that only does socket I/O, for HTTP servers, one milestone at a time. Each step is measured against ringzero, a C server on io_uring (liburing).

**Status: milestone 0.** `/dev/rzio` exists, each `open()` gets its own context, and two ioctls work (`INFO` and `PING`). There are no sockets yet. The plan is in [docs/roadmap.md](docs/roadmap.md).

## Requirements

- Linux on x86_64, with KVM for the test VM
- Headers for the running kernel: `sudo apt install linux-headers-$(uname -r)`
- `gcc`, `make` and static glibc (`build-essential` has all three)
- virtme-ng, which runs the test VM: `sudo apt install virtme-ng`
- On Ubuntu, the kernel image is readable only by root, and virtme-ng needs to read it. Redo this after every kernel update:

  ```sh
  sudo chmod 644 /boot/vmlinuz-$(uname -r)
  ```

## Build

```sh
make          # module/rzio.ko and tools/rzio_test
make check    # kernel coding style (checkpatch --strict)
```

On Ubuntu the build prints two harmless warnings:
- "the compiler differs from the one used to build the kernel": it's the same gcc version from a different package build.
- "Skipping BTF generation": the headers package has no `vmlinux`.

## Test in a VM (do this first)

```sh
make vm-test
```

This boots your running kernel in a throwaway QEMU VM, which takes about 3 seconds. It loads the module, runs `tools/rzio_test`, unloads the module, and fails on any kernel warning or new taint flag. A passing run ends like this:

```
/dev/rzio: abi 0, ctx 1
ctx 1: 3 pings counted
ctx 2: separate state
unknown command rejected
ok
--- kernel log
[    2.490511] rzio: /dev/rzio ready, abi 0
[    2.497647] rzio: ctx 1 opened by pid 271
[    2.498761] rzio: ctx 2 opened by pid 271
[    2.498764] rzio: ctx 2 released after 1 pings
[    2.498766] rzio: ctx 1 released after 3 pings
[    2.513071] rzio: unloaded
PASS
```

To work in the VM yourself, open a root shell there. Leave with Ctrl-D.

```sh
make vm-shell
insmod module/rzio.ko dyndbg=+p   # dyndbg=+p turns on the pr_debug() lines
tools/rzio_test
dmesg
rmmod rzio
```

The VM sees this directory read-only. It sees the rest of the host filesystem through an overlay that disappears when the VM exits. A crash takes down only the VM.

You can tune the VM with environment variables:

| Variable | Default | What it does |
|---|---|---|
| `RZIO_VM_PIN` | no pinning | Host CPUs that QEMU runs on, e.g. `16-31` for the E-cores of an i9-14900K |
| `RZIO_VM_CPUS` | `4` | Guest CPUs |
| `RZIO_VM_MEM` | `2G` | Guest memory |

## Load on the host

Only do this after `make vm-test` passes. On the host, a bug in the module crashes the whole machine.

```sh
sudo insmod module/rzio.ko
ls -l /dev/rzio          # crw------- root root
sudo tools/rzio_test
sudo rmmod rzio
```

- **Taint.** Loading marks the kernel as tainted with `O` (out-of-tree module) and `E` (unsigned module). That's expected.
- **Secure Boot.** With Secure Boot on, the kernel refuses unsigned modules with "Key was rejected by service". Either sign the module with a key enrolled through MOK, or stay in the VM.
- **Unloading.** `rmmod` fails with "Module rzio is in use" while any process has `/dev/rzio` open.

### Install it so `modprobe` and boot can load it

```sh
make
sudo make -C /lib/modules/$(uname -r)/build M=$PWD/module modules_install
sudo depmod -a                                       # modules_install skips this with Ubuntu's headers
sudo modprobe rzio
echo rzio | sudo tee /etc/modules-load.d/rzio.conf   # optional: load at every boot
```

The module goes to `/lib/modules/$(uname -r)/updates/rzio.ko`. A module only loads on the kernel it was built for, so rebuild and reinstall after every kernel update. To remove it:

```sh
sudo modprobe -r rzio
sudo rm -f /lib/modules/$(uname -r)/updates/rzio.ko /etc/modules-load.d/rzio.conf
sudo depmod -a
```

### Let non-root users open it

`/dev/rzio` is root-only (mode 0600). To give a group access, add a udev rule, then reload the module and log in again:

```sh
echo 'KERNEL=="rzio", GROUP="rzio", MODE="0660"' | sudo tee /etc/udev/rules.d/99-rzio.rules
sudo groupadd -f rzio && sudo usermod -aG rzio $USER
```

## Use from Docker

Containers share the host's kernel. Load the module on the host as above, never inside a container. Then pass the device in:

```sh
docker run --rm --device /dev/rzio -v "$PWD/tools:/rzio:ro" ubuntu:24.04 /rzio/rzio_test
```

- **No extra privileges.** You don't need `--privileged` or a seccomp change. rzio uses only `open` and `ioctl` (later also `mmap`), and Docker's default profile allows all three.
- **Permissions.** The process in the container must be root, or in the device's group.
- **Any image.** `tools/rzio_test` is statically linked, so it runs in any image.

## Layout

```
module/rzio.c       the kernel module
module/Kbuild       how kbuild builds it
include/rzio.h      user-space ABI (ioctl numbers and structs), used by both sides
tools/rzio_test.c   smoke test
scripts/vm.sh       VM runner (virtme-ng)
docs/roadmap.md     design and milestones
```

## License

GPL-2.0. A module must have a GPL-compatible license to use the kernel symbols exported with `EXPORT_SYMBOL_GPL`. `include/rzio.h` adds the Linux-syscall-note, so programs under any license can include it.
