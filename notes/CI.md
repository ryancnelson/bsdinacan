# Woodpecker verification and failure diagnosis

Discover the CI server URL and repository ID from the GitHub commit status
links. Set `ci_url` and `repo_id` for those coordinates; do not publish private
service addresses in tracked documentation.
`ci`, `mac68k`, and `mac-automation` must succeed for the exact candidate SHA.
A successful local build does not establish that its pushed commit passed CI.

## Read actual workflow results

GitHub commit status supplies links to each workflow. If its aggregate is
lagging, read the exact Woodpecker pipeline and inspect its commit and workflow
states. Pipeline numbers and database IDs are different identifiers.

```sh
curl -fsSL "$ci_url/api/repos/$repo_id/pipelines/$pipeline_number"
```

Each workflow's `children` list contains step `id`, `pid`, name and state.
To read a failed step, use its **id**, not its workflow-local pid:

```sh
curl -fsSL "$ci_url/api/repos/$repo_id/logs/$pipeline_number/$step_id" |
  python3 -c 'import base64,json,sys; [print(base64.b64decode(r["data"]).decode("utf-8", "replace")) for r in json.load(sys.stdin) if r.get("data")]'
```

Inspect the first meaningful failure. A rerun cannot replace fixing a
reproducible failure; an expected negative feasibility diagnostic must remain
clearly labeled as such, separate from successful application builds.

## Concurrent local workflows

The Linux agent uses a local backend, so independent workspaces can share host
`/tmp`. Host-side tests must use `mktemp` directories and cleanup traps, never
fixed stderr capture filenames. CI-01 fixed an observed cross-pipeline race in
the printenv test. Paths inside cannedBSD's own per-process filesystem are a
different namespace and do not collide between independent test processes.

## Mac artifacts and acceptance

Use the artifact directory for the exact pipeline number and full commit, and
verify `commit.txt` and `SHA256SUMS` before staging. The ordinary application
archive and optional diagnostic archives are distinct outputs.

The coordinator owns the single Basilisk II guest slot. Use the checked-in
`platform/mac68k/guest.py` staging protocol and Hammerspoon runner, with local
non-cloud runtime storage. Require the exact fresh expected transcript and
mode-specific screenshot/completion evidence; normal guest shutdown precedes
autorun acceptance publication and slot release. A build or old screenshot
is not a guest result. See the platform and automation READMEs for commands.

## Solaris 9 SPARC requirement and transition

User direction on 2026-09-08 makes the existing Solaris 9 sun4m rig a required
portability target alongside Linux and System 7. Its 32-bit big-endian SPARC
ABI and GCC 3.4.6 toolchain must exercise shared behavior, not only a hello-world
build. See SOLARIS-01 and SOLARIS-02 at the front of BACKLOG.md.

The earlier local port is retained on `work/SOLARIS-01-reference` at
`a28f9ed8b369b37547df8fa62c08d3565bad8f23`. It is reference material, not proof
that current main passes. The rig was found running when this policy was added;
its console and disks may already have an owner. Private operational coordinates
are in `~/wiki/hosts/cannedbsd-solaris9.md` on the documentation workstation.
The coordinator supplies these to the assigned worker without committing secrets
or personal infrastructure paths into the public repository.

Until SOLARIS-01 lands, affected workers continue their assigned IDs and existing
Linux/Mac checks, and explicitly report Solaris acceptance as pending integration.
The coordinator must carry that outstanding gate forward; a merge during this
bootstrap is not full Solaris qualification. Prioritize SOLARIS-01 instead of
adding unrelated compatibility features to each branch.

After SOLARIS-01 is accepted, shared runtime/libc/VFS/shell/command/ABI changes
require a clean native guest build and the applicable complete runtime suite on
the exact candidate commit before integration. Record source archive SHA256,
compiler versions, every command and probe exit, fresh full transcript and final
PASS marker. Preserve the one-host-process model and existing error contracts.
Documentation-only and strictly Linux- or Mac-specific changes may record Solaris
acceptance as not required with a concrete reason. Solaris-specific code requires
Solaris execution. Linux-only /proc and sanitizer checks stay on Linux.

Before SOLARIS-02, this is serialized manual acceptance using the same gate that
CI will invoke. After SOLARIS-02, require the distinct `solaris9` CI status on the
exact commit as well as the existing statuses. Never describe a future workflow
as installed, or treat missing infrastructure, timeout or skipped work as success.
The coordinator serializes all guest console/media use until a tested shared
lock enforces ownership. Other workers must not alter an active rig.
