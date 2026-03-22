# GitHub Wiki sync (Actions)

The workflow [.github/workflows/sync-wiki.yml](workflows/sync-wiki.yml) pushes the `wiki/` folder to the repository wiki.

## If the job fails: `repository not found` / `Wiki repository not found`

GitHub does **not** create `https://github.com/<owner>/<repo>.wiki.git` until the wiki is turned on and at least one page exists.

1. Open the repo on GitHub → **Settings** → **General** → **Features**.
2. Enable **Wikis**.
3. Open the **Wiki** tab → **Create the first page** (e.g. title `Home`, any body) → **Save**.

Re-run the failed workflow (**Actions** → **Sync wiki** → **Run workflow**) or push another change under `wiki/`.

## Token

The workflow uses `secrets.WIKI_DEPLOY_TOKEN` if set, otherwise `secrets.GITHUB_TOKEN`.

If the deploy step still fails with **permission denied** or **403**, create a [fine-grained PAT](https://github.com/settings/tokens?type=beta) or classic PAT with **Contents** read-write on this repository, add it as repository secret **`WIKI_DEPLOY_TOKEN`**, and re-run.

Wiki URL for this repo: <https://github.com/kennetholsenatm-gif/qminiwasm-core/wiki>
