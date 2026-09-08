# VFS-01: two-mount routing boundary

- Status: Done
- Base SHA: 873807d
- Branch: `work/VFS-01`
- Hypothesis: path traversal can cross a mount boundary without exposing a filesystem-specific node to tasks or descriptors.

## Red

- Command: `make test`
- Expected failure: `cross-mount mkdir did not route to the second mount`
- Observed failure: `FAIL: cross-mount mkdir did not route to the second mount`

## Green

- Focused command: `make test`
- Full command: `make ci`
- Linux Woodpecker: success
- mac68k Woodpecker: success
- Guest acceptance, when required: guest acceptance pending coordinator

## Change and review

- Implementation: Added `cb_vfs_mount_path` API to allow registering additional mount points. Updated `resolve_normalized` and `cwd_string` to automatically transition across mount point boundaries without requiring filesystem-specific code in path traversal. Protected mount points from being unlinked.
- ABI, ownership, and cleanup review: Added `mounts` tracking array to `cb_kernel`. Ensured `cb_vfs_destroy` properly destroys mounted filesystems and releases the mount points. Failed mount installation cleans up gracefully.
- Documentation: No changes to external user documentation required.
- Remaining risk or follow-up: Only 4 mount points are supported due to fixed array size in `cb_kernel`.
