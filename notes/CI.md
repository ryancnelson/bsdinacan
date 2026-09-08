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
