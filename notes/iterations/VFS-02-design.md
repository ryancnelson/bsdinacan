# VFS-02: Executable VFS Nodes

## Hypothesis
Registered programs can appear as executable VFS objects, and shell lookup can resolve those objects natively via the file system instead of relying on a hidden internal path registry.

## Design & Semantics

### Runtime-Visible Executable Nodes
- **Node Type:** A new node type `CB_NODE_EXECUTABLE = 5` is introduced to `struct cb_stat_v1` to represent executable objects in the VFS.
- **VFS Path:** `cb_kernel_register` and `cb_kernel_register_executor` are strictly preserved to maintain API compatibility. Instead of appending to a deprecated `kernel->programs` array, these functions atomically create a `CB_NODE_EXECUTABLE` node at `/bin/<name>`. `cb_vfs_initialize` runs before host registration and creates the `/bin` directory.
- **Node Data:** The internal state of a `CB_NODE_EXECUTABLE` node will securely store the backing `struct cb_program` pointer.
- **Registration Policy:** Attempting to register a program with a name containing a slash (`/`) is rejected. Registering a name that already exists is rejected. This matches the semantic constraints of the current API.

### Exact Node Operations Behavior
- **read:** Always returns `0` (EOF).
- **stat:** Returns size `0`, type `CB_NODE_EXECUTABLE`, and mode `0555`.
- **write:** Always returns `-CB_EPERM`.
- **truncate:** Always returns `-CB_EINVAL`.

### Exact Ownership and Release Policy
A retained node alone is not a complete program ownership policy. The exact lifecycle is:
1. **Registration:** `cb_kernel_register_executor` prepares the `cb_program`. It attempts to create the VFS node in `/bin/`. If node creation fails (e.g., allocation failure), `cb_executor_program_destroy` is called immediately, and registration returns `-1`. If successful, the VFS node takes exclusive ownership of the `cb_program`.
2. **Failed Spawn/Exec:** If path resolution fails, or argument/action allocation fails during preparation, the temporary VFS node reference is cleanly dropped (`cb_vfs_node_release`) and the error cascades.
3. **Successful Spawn/Exec:** `struct cb_task` gains an `executable_node` field. On spawn, the task retains the node. On exec, the task retains the new node as `pending_executable_node`. Once the transition completes, it releases the old `executable_node` and commits the new one.
4. **Task Teardown (Task Destroy):** `api_exit` does *not* release the `executable_node` because the task remains a zombie, and `task->execution` still exists. A custom executor can rely on `cb_program` existing during its cleanup. Instead, the `executable_node` is strictly released in `task_destroy`, after `cb_executor_instance_destroy` is invoked.
5. **Kernel Destroy:** Pending `executable_node` references are cleaned up, and VFS teardown explicitly drops all node references. When the `CB_NODE_EXECUTABLE` reference count reaches exactly `0` (after all instances are destroyed), its `destroy` operation calls `cb_executor_program_destroy`.

### Bare-name Resolution Compatibility
To maintain backward compatibility:
- If `api_spawn`, `api_exec`, or `cb_kernel_boot` is given a target containing no slashes (e.g., `"sh"`), it implicitly prefixes `/bin/` before VFS resolution. 
- If the target contains a slash (e.g., `/bin/sh` or `./sh`), it is resolved exactly as provided.
- **ENOENT / EACCES / ENOEXEC:** A missing path yields `CB_ENOENT`. Spawning a directory or terminal yields `CB_EACCES`. Spawning a `CB_NODE_REGULAR` file yields `CB_ENOEXEC`.

## Implementation & Test Sequence

1. **Red Test:** Add a test asserting that `api->stat("/bin/sh")` returns `CB_ENOENT`. Add a test asserting that `api->spawn("/missing/sh")` returns `CB_ENOENT`. On current code, these assertions will fail because `/missing/sh` incorrectly succeeds due to path-stripping in `program_find`.
2. **Core VFS Changes:** Introduce `CB_NODE_EXECUTABLE` and the exact `read`/`stat`/`write`/`truncate` behaviors. Update RAMFS to support creating and storing executable nodes.
3. **Registration & Atomicity:** Modify `cb_kernel_register_executor` to create `/bin/<name>`, enforcing slash/duplicate rejection. Add strict tests verifying allocation failure rolls back `cb_program` creation cleanly.
4. **Execution Wire-up:** Modify `api_spawn` and `api_exec` to use `cb_vfs_open` or node-lookup to resolve paths (with the `/bin/` fallback). Update the task's `executable_node` references.
5. **Shell Migration:** Update `src/shell.c` execution logic.
6. **Destruction Order & Unlink Matrix Test:** Add a deterministic matrix test:
   - *Failed preparation rollback:* Inject OOM during `spawn` argv processing. Assert the node reference is completely released.
   - *Running Unlink Destruction Order:* Spawn a yielding process with a mock executor, unlink its `/bin/` node, and let it exit. Assert that `cb_executor_instance_destroy` completes *before* `cb_executor_program_destroy`.
   - *Exec Unlink Destruction Order:* Queue an `api_exec` transition, unlink the new node while it is in `CB_TASK_EXEC_PENDING`. Assert the host executor successfully resumes, the exec completes, and the old program instance is destroyed safely.
7. **Cleanup:** Delete `program_find` and `kernel->programs`.
8. **Green Verification:** Ensure all existing commands continue to work, exact tests pass, and CI memory checks are flawless.
