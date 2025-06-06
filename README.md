# taskd – Minimal Task Daemon for Firecracker MicroVMs

**`taskd`** is a minimal vsock-based daemon written in pure C for lightweight task orchestration within Firecracker microVMs. It forms the internal agent of a Gym-compatible environment for reinforcement learning over Linux task execution.

This daemon is designed for **high-performance parallelization** across isolated, disposable VM instances. It is built for integration with a host-side training loop that communicates via vsock to set up, monitor, and verify system-level tasks.

---

## ✴️ TL;DR

> A tiny, public domain daemon that:
> - Receives a task description over vsock
> - Applies the task inside the VM (e.g. create file, install package, simulate failure)
> - Reports back on setup readiness and task success/failure
> - Runs forever inside the guest, stateless between VMs

---

## ✳️ Features

- **Stateless execution:** Each `taskd` instance lives in a throwaway VM with no persistent state.
- **Simple protocol:** JSON-structured binary messages over vsock: one for setup, one for per-timestep feedback.
- **Tiny footprint:** Pure C, minimal syscalls, no external dependencies.
- **Compatible:** Works with `extended_firecracker_parallel.py` to run 256+ concurrent microVMs.
- **Unlicensed:** Public domain, use or fork freely.

---

## 🔧 Integration

### 1. Host Side: `extended_firecracker_parallel.py`
This Python script spins up multiple Firecracker microVMs and:
- Mounts a prebuilt rootfs containing `taskd`
- Connects via vsock to send a structured task description
- Verifies correct response (e.g. `READY`, `DONE`, etc.)
- Tears down VM, leaving no traces

### 2. Guest Side: `taskd`
Launched automatically as PID 1 or via init system stub. Listens on a fixed vsock port (usually `52`) and waits for commands.

---

## 📡 Protocol

### Message 1: Setup Task

- JSON or fixed-format binary structure
- Specifies:
  - Task type (e.g. file op, permission bug, install package)
  - Parameters (paths, user, script contents, etc.)

**Daemon replies with**:
- `READY` when task setup is complete
- `FAIL` if setup failed

### Message 2+: Timestep Poll

- Trainer queries status periodically
- Daemon replies with:
  - `DONE` – task completed
  - `INPROGRESS` – still executing
  - `ERROR:<desc>` – task failed

---

## 📂 Filesystem Layout

Inside the rootfs used by the microVMs:

`/sbin/taskd` # The daemon binary (statically linked)
`/tmp/` # Workspace for ephemeral task setup
`/root/task.log` # Optional log for debugging


---

## 🔬 Development Status

| Component | Status     |
|----------|-------------|
| Basic vsock daemon | ✅ Done |
| Task setup and report loop | 🔄 WIP |
| Protocol spec | 🔄 WIP |
| Task execution core | 🚧 Stubbed per task type |
| Logging/debug hooks | ✅ Minimal |
| Public domain licensing | ✅ Unlicense |

---

## ⚠️ Warning

This daemon is for research and testing only. It performs privileged operations inside the guest VM. Do **not** run it outside an isolated virtual machine.

---

## 👤 Author

Research direction: **Alistair Nequa**  
Implementation: **ChatGPT (June 2025)**  
License: [The Unlicense](https://unlicense.org)

> *"Into the public domain it goes, and may it never return."*
