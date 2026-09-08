# VFS-02: Executable VFS Nodes

## Hypothesis
Registered programs can appear as executable VFS objects, and shell lookup can resolve those objects natively via the file system instead of relying on a hidden internal path registry (`program_find`).

## Design & Semantics

### Runtime-Visible Executable Nodes
- **Node Type:** A new node type `CB_NODE_EXECUTABLE` is introduced to `struct cb_stat_v1` to represent executable objects in the VFS.
- **Location:** Executables will be mounted or placed in a standard path (e.g., `/bin/`). `vfs_initialize` will construct `/bin` and create executable nodes for base programs (`sh`, `echo`, `cat`, etc.) within it.
- **Node Data:** The internal state of a `CB_NODE_EXECUTABLE` node will securely store a pointer to the backing `struct cb_program`.

### Permission and Error Distinctions
- **ENOENT:** Attempting to `spawn` or `exec` a path that does not exist in the VFS natively returns `CB_ENOENT`.
- **EACCES / ENOEXEC:** 
  - If `spawn`/`exec` is called on a path that resolves to a `CB_NODE_DIRECTORY` or `CB_NODE_TERMINAL`, it returns `CB_EACCES`.
  - If `spawn`/`exec` is called on a `CB_NODE_REGULAR` file that is not structurally valid as an executable (since CannedBSD does not support ELF parsing or arbitrary bytecode yet), it returns `CB_ENOEXEC`.
  - A successful resolution requires the target node to be `CB_NODE_EXECUTABLE`.

### Open and Unlink Lifetime
- **Open:** Executable nodes can be `open`ed with `CB_O_RDONLY` to read their basic identity or size (returning 0 or a nominal stub), but attempting to `write` or `truncate` them will return `CB_EPERM` or `CB_EINVAL`.
- **Unlink:** Executable nodes can be `unlink`ed from RAMFS like any other node. The `cb_program` pointer inside the node is valid for the lifetime of the node.
- **Reference Counting:** A running task spawned from an executable node will retain a reference to the `cb_vfs_node`, ensuring that even if `unlink` is called, the node (and its underlying `cb_program`) is not destroyed until the task exits.

### Registry Cleanup and Atomic Spawn/Exec
- **Deprecation of Global Registry:** The `kernel->programs` array and `program_find` function are removed entirely.
- **Atomic Operations:** `api_spawn` and `api_exec` will resolve the absolute/relative path precisely once. They will acquire a VFS reference (`cb_vfs_node_retain`) to the target executable, ensuring the backing program cannot be torn down concurrently.
- **Shell Resolution:** The shell's execution logic will rely on `$PATH` parsing or direct path resolution to find executables instead of searching the old `program_find` registry.

## Implementation & Test Sequence

1. **Red Test:** Add tests attempting to `stat("/bin/sh")` and `open("/bin/sh")`, asserting they currently fail. Attempt to `spawn("/bin/sh")` expecting it to fail since `/bin/sh` does not exist as a file.
2. **Core VFS Changes:** Introduce `CB_NODE_EXECUTABLE` to headers. Update RAMFS to support creating and storing executable nodes.
3. **Execution Wire-up:** Modify `api_spawn` and `api_exec` to use `cb_vfs_open` (or a similar node-lookup equivalent) to resolve paths, check for `CB_NODE_EXECUTABLE`, and extract the `cb_program`.
4. **VFS Initialization:** Modify `cb_kernel_create` / `vfs_initialize` to populate `/bin/` with `CB_NODE_EXECUTABLE` instances instead of pushing to `kernel->programs`.
5. **Shell Migration:** Update `src/shell.c` to search `/bin/` or resolve absolute paths instead of iterating the deprecated global registry.
6. **Cleanup:** Delete `cb_kernel_register`, `program_find`, and the internal `kernel->programs` state.
7. **Green Verification:** Ensure all existing commands continue to work, and the new tests correctly validate stat, access errors, and atomicity guarantees.
