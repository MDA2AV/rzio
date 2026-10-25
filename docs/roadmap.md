# Roadmap

Each milestone teaches one part of the kernel and ends with something you can run or measure.

Benchmarks run inside the VM can only be compared with other runs in the same VM. So every comparison is an A/B there: rzio against ringzero on io_uring, same kernel, same pinning.

## Design

rzio copies ringzero's model: one reactor per thread, with no locks between reactors.

- **Device.** `/dev/rzio` is a misc device. Each `open()` is one reactor context, like one io_uring instance per reactor thread.
- **Shared memory.** A setup ioctl plus `mmap` share three regions with user space: a submission queue, a completion queue and a buffer pool.
- **Connections** are IDs inside the context, not file descriptors.

```
packet → TCP (softirq) → rzio's sk_data_ready: mark the connection ready, wake the owner thread
owner: ioctl(ENTER) → accept/recv into the shared pool → post completions → return
owner: run the handler → write the response into the pool → post SEND → ioctl(ENTER) → kernel sends
```

Two early decisions:

- **User space creates the listening socket** and passes its fd in; the module looks it up with `sockfd_lookup()`. The socket then belongs to the caller's network namespace. If the module created it with `sock_create_kern(&init_net, ...)`, a container would end up listening on the host's network.
- **Accepts, receives and sends run inside the owner's `ENTER` ioctl**, not on a kernel worker. This is io_uring's DEFER_TASKRUN model. The socket callbacks stay tiny, almost nothing needs a lock, and sockets accepted in the caller's context count against its container's memory limit. Sockets accepted on a kernel worker would escape that limit.

## Milestones

### 0. Skeleton (done)

A misc device, a context per `open()`, the `INFO` and `PING` ioctls, and the VM test.

- **Learn:** kbuild, module and file lifetimes, `copy_to_user`/`put_user`, `GFP_KERNEL_ACCOUNT`.
- **Done when:** `make vm-test` passes.

### 1. In-kernel plaintext responder

A kernel thread accepts connections and answers `GET /plaintext` with no user space involved at all. Write it blocking first (like ksmbd), then callback-driven (like nvmet-tcp).

- **Learn:** the kernel socket API (`kernel_accept`, `kernel_recvmsg`, `kernel_sendmsg`), softirq vs process context, workqueues.
- **Done when:** you have wrk numbers for it next to ringzero in the same VM. That is the ceiling for "everything in ring 0".
- **Before you start:** switch the VM to a debug kernel (see below).

### 2. The rings

Setup and `mmap` for the submission and completion queues, a no-op operation, and `ENTER` with waiting.

- **Learn:** `vmalloc_user` + `remap_vmalloc_range`, and `smp_store_release`/`smp_load_acquire`. Also treating shared memory as hostile: `READ_ONCE`, bounds checks on every index, `array_index_nospec`.
- **Done when:** user space round-trips a million no-ops, and a fuzzer writing garbage into the rings can't crash the kernel.

### 3. Sockets through the rings

Register a listening fd, then add multishot accept and recv into the buffer pool, send, and close. Port ringzero onto rzio; its structure maps closely.

- **Done when:** ringzero on rzio serves wrk with zero errors, next to ringzero on io_uring in the same VM.

### 4. Make it survive

- Partial sends (`sk_write_space`)
- Peer FIN/RST (`sk_state_change`)
- A full completion queue
- `kill -9` under load, and heavy connection churn with KASAN and lockdep on
- Running in Docker with `--device /dev/rzio`

**Done when:** KASAN and lockdep stay quiet under churn, and ringzero on rzio runs in a container.

### 5. Experiments

Measure each one with perf against io_uring:

- `tcp_read_sock()` with a read callback instead of recvmsg
- a kernel polling thread, like SQPOLL
- zero-copy send with `MSG_SPLICE_PAGES`

## Debug kernel (from milestone 1)

The Ubuntu kernel has no KASAN, lockdep or `DEBUG_ATOMIC_SLEEP`. Its KFENCE catches memory bugs only some of the time. Once rzio touches sockets, build your own kernel:

- **Disk and packages.** It needs about 10 GB free and these packages: `flex bison libelf-dev libssl-dev dwarves`.
- **Config.** In a kernel tree, `vng --build --config <snippet>` builds a small VM kernel. The snippet should turn on `KASAN`, `PROVE_LOCKING` and `DEBUG_ATOMIC_SLEEP`, and keep `IO_URING` so the A/B against ringzero still works.
- **Script changes.** Build the module against that tree with `KDIR=<tree> make`, and teach `scripts/vm.sh` to boot that kernel instead of the host's.

## Kernel code to read

- [`drivers/nvme/target/tcp.c`](https://elixir.bootlin.com/linux/v6.14/source/drivers/nvme/target/tcp.c): a fast, callback-driven TCP server in the kernel. Read its `data_ready`, `write_space` and `state_change` callbacks, how it saves and restores them, and its `io_work` loop.
- [`fs/smb/server/transport_tcp.c`](https://elixir.bootlin.com/linux/v6.14/source/fs/smb/server/transport_tcp.c) (ksmbd): the simple blocking accept loop. ksmbd has also had several remotely exploitable CVEs, which is the security lesson.
- [`io_uring/net.c`](https://elixir.bootlin.com/linux/v6.14/source/io_uring/net.c) and [`io_uring/kbuf.c`](https://elixir.bootlin.com/linux/v6.14/source/io_uring/kbuf.c): multishot recv and provided buffers, which rzio competes with.

## Rules that prevent most crashes

- **Socket callbacks can run in softirq context.** Never sleep in them; queue work and wake the owner.
- **Restore the original callbacks** under `sk_callback_lock` before releasing a socket.
- **Protect what callbacks point to.** The connection object needs a refcount or RCU. This is the classic use-after-free spot.
- **Distrust user input.** Everything that comes from user space or shared memory is hostile: copy it once, then validate it.
- **Keep `MODULE_LICENSE("GPL")`.** RCU, lockless lists and other helpers are exported to GPL modules only.
