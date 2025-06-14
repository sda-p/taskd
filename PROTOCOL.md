# taskd Protocol

This document describes how to communicate with the `taskd` daemon over a
vsock connection. The protocol uses JSON messages and a simple two step
interaction.

## 1. Handshake

The first message sent by the client must contain a greeting and a protocol
version. The daemon validates the JSON using `parse_handshake()` defined in
`protocol.h`.

```json
{ "hello": "string", "version": 1 }
```

If the message is well formed the daemon replies with a status payload created
by `report_status()`. Status `0` means the handshake succeeded, otherwise `-1`
indicates failure.

```json
{ "status": 0 }
```

On a failed handshake the connection is closed immediately.

## 2. Recipe upload

After a successful handshake the client sends a JSON array describing a recipe.
Each array element contains an opcode and a data object. Opcodes correspond to
`sm_opcode` values from `state_machine.h`.

```json
[
  { "op": "SM_OP_LOAD_CONST", "data": { "dest": 0, "value": "example" } },
  { "op": "SM_OP_FS_CREATE", "data": { "dest": 1, "path": 0, "type": 2 } }
]
```

`proto_parse_recipe()` converts this array into a linked list of instructions for
the state machine. Once parsed, the chain is submitted with `sm_submit()` and the
connection is closed. Execution happens asynchronously in the worker thread.

## Registers and operations

The state machine owns eight general purpose registers as defined in
`state_machine.c`.

Values loaded by `SM_OP_LOAD_CONST` can be referenced by later instructions via
register indices. The `value` field may be either a JSON string or number.
Numeric values are stored directly in the register as integers. File system instructions (create, copy, move, etc.) use these
registers for their parameters and store success (non-zero) or result pointers in
a destination register.

Additional opcodes provide utility functions:

- `SM_OP_FS_HASH` – compute an xxHash64 of the file at `path`.
- `SM_OP_FS_LIST` – list directory entries separated by newlines.
- `SM_OP_SHELL` – execute the command string in register `cmd` and store its
  output in `dest`.
- `SM_OP_EQ` – compare two registers for equality.
- `SM_OP_NOT` – logical negation of a register value.
- `SM_OP_AND` / `SM_OP_OR` – logical conjunction/disjunction of two registers.
- `SM_OP_INDEX_SELECT` – extract the item at `index` from a newline separated
  string in `list`.
- `SM_OP_RANDOM_RANGE` – store a pseudo-random integer between `min` and `max`
  (inclusive) in `dest`.
- `SM_OP_PATH_JOIN` – join `base` and `name` with a slash and store the new
  path in `dest`.
- `SM_OP_RANDOM_WALK` – starting from `root`, walk `depth` random directory
  levels and store the resulting path in `dest`.
- `SM_OP_DIR_CONTAINS` – set `dest` to non-zero if directory `a` is fully
  contained within directory `b`.
- `SM_OP_RAND_SEED` – set the pseudo-random seed to `seed` for future
  operations.
- `SM_OP_REPORT` – send a protocol message back to the host containing the
  contents of the specified registers as a JSON array.

`SM_OP_REPORT` requires a `regs` field listing which register indices should be
included in the outbound message. When executed, the daemon builds a JSON object
`{"values": [ ... ]}` and sends it over the active vsock connection before
continuing with the recipe.

## Example recipe

Below is a minimal recipe that creates an empty file `/tmp/hello.txt`.

```json
[
  { "op": "SM_OP_LOAD_CONST", "data": { "dest": 0, "value": "/tmp/hello.txt" } },
  { "op": "SM_OP_LOAD_CONST", "data": { "dest": 1, "value": "file" } },
  { "op": "SM_OP_FS_CREATE", "data": { "dest": 2, "path": 0, "type": 1 } }
]
```

The first two instructions load constants into registers `0` and `1`. The third
instruction references those registers to create the file. Register `2` will
contain `1` on success or `0` on failure once the worker thread executes the
recipe.

## Responses

Once a recipe has been submitted the daemon accumulates every report message and
the final status into a single JSON array. The array elements are the individual
JSON objects generated during execution. A simple run might therefore produce:

```json
[
  {"values": ["ok"]},
  {"status": 0}
]
```

Clients should iterate over this array in order. The initial handshake status is
still sent as a standalone message before the recipe upload.
