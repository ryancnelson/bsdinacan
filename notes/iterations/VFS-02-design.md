# VFS-02: Executable VFS Nodes

## Hypothesis
Registered programs can appear as executable VFS objects, and shell lookup can resolve those objects natively via the file system instead of relying on a hidden internal path registry.

## Design & Semantics

### Runtime-Visible Executable Nodes
- **Node Type:** A new node type `CB_NODE_EXECUTABLE` is introduced to `struct cb_stat_v1` to represent executable objects in the VFS.
- **VFS Path:** `cb_kernel_register` and `cb_kernel_register_executor` are strictly preserved to maintain API compatibility with existing host adapters. Instead of appending to a deprecated `kernel->programs` array, these functions will atomically create a `CB_NODE_EXECUTABLE` node at `/bin/<name>`. Since `cb_vfs_initialize` runs before host registration, it will create the `/bin` directory, enabling subsequent registration calls to populate it.
- **Node Data:** The internal state of a `CB_NODE_EXECUTABLE` node will securely store the backing `struct cb_program` pointer.

### Exact Node Operations Behavior
- **read:** Always returns `0` (EOF).
- **stat:** Returns size `0`, type `CB_NODE_EXECUTABLE`, and mode `0555`.
- **write:** Always returns `-CB_EPERM`.
- **truncate:** Always returns `-CB_EINVAL`.

### Exact Ownership and Release Policy
A retained node alone is not a complete program ownership policy. The exact lifecycle is:
1. **Registration:** `cb_kernel_register_executor` prepares the `cb_program`. It attempts to create the VFS node in `/bin/`. If node creation fails (e.g., allocation failure), `cb_executor_program_destroy` is called immediately, and registration returns `-1`. If successful, the VFS node takes exclusive ownership of the `cb_program`.
2. **Failed Spawn/Exec:** If path resolution fails or finds an invalid type, the temporary VFS node reference is dropped (`cb_vfs_node_release`).
3. **Successful Spawn/Exec:** `struct cb_task` gains an `executable_node` field. On spawn, the task retains the node. On exec, the task retains the new node as `pending_executable_node`. Once the transition completes, it releases the old `executable_node` and commits the new one.
4. **Task Exit:** `api_exit` releases the `executable_node` reference.
5. **Unlink:** Calling `unlink` removes the `/bin/` directory entry and drops the parent's reference to the node. If the program is actively executing, the task's `executable_node` reference keeps the node and its backing `cb_program` alive.
6. **Kernel Destroy:** Tearing down the VFS drops all directory references. Task teardown drops execution references. When the `CB_NODE_EXECUTABLE` reference count reaches exactly `0`, its `destroy` operation fires, which calls `cb_executor_program_destroy` to free the underlying program.

### Bare-name Resolution Compatibility
To maintain compatibility with host tests and avoiding coupling core API to shell `$PATH` mechanics:
- If `api_spawn`, `api_exec`, or `cb_kernel_boot` is given a target containing no slashes (e.g., `"sh"`), it implicitly prefixes `/bin/` before VFS resolution. 
- If the target contains a slash (e.g., `/bin/sh` or `./sh`), it is resolved exactly as provided.
- **ENOENT / EACCES / ENOEXEC:** A missing path yields `CB_ENOENT`. Spawning a directory or terminal yields `CB_EACCES`. Spawning a `CB_NODE_REGULAR` file yields `CB_ENOEXEC`.

## Implementation & Test Sequence

1. **Red Test:** Prove the current `program_find` ignores paths by asserting that `api->spawn("/missing/sh")` actually succeeds (incorrect false-path execution). Additionally, assert that `api->stat("/bin/sh")` currently fails with `CB_ENOENT`.
2. **Core VFS Changes:** Introduce `CB_NODE_EXECUTABLE` and the exact `read` / `stat` / `write` / `truncate` behaviors. Update RAMFS to support creating and storing executable nodes.
3. **Registration & Atomicity:** Modify `cb_kernel_register_executor` to create `/bin/<name>`. Add a strict allocation-failure test verifying that if node creation fails, the program is cleanly destroyed without memory leaks.
4. **Execution Wire-up:** Modify `api_spawn` and `api_exec` to use `cb_vfs_open` or node-lookup to resolve paths (with the `/bin/` bare-name fallback). Extract the `cb_program` from the node and update the task's `executable_node` reference.
5. **Shell Migration:** Update `src/shell.c` execution logic.
6. **Unlink Matrix Test:** Add a deterministic matrix test:
   - *Running Unlink:* Spawn a yielding process, unlink its `/bin/` node mid-execution, and assert it continues and exits with `0`.
   - *Queued Exec Unlink:* Queue an `api_exec` transition, unlink the node while it is in `CB_TASK_EXEC_PENDING`, and assert the host executor successfully resumes and completes the exec.
7. **Cleanup:** Delete `program_find` and the internal `kernel->programs` state entirely.
8. **Green Verification:** Ensure all existing commands continue to work, false paths are rejected (`/missing/sh` -> `ENOENT`), and strict exact CI memory tests pass.
