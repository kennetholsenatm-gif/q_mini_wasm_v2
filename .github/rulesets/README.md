# Repository rulesets (security rules)

These JSON rulesets define **branch protection and security rules** that appear under **GitHub Settings → Rules → Rulesets**. Import them so merges to the default branch require CI and pull requests.

## What’s included

- **`branch-protection-security.json`**  
  - Applies to `main` and `master`.  
  - **Require pull request**: changes must go through a PR (no direct push).  
  - **Require status checks**: CI jobs `lint-and-test` and `security` must pass (see [.github/workflows/ci.yml](../workflows/ci.yml)).  
  - **Block force push**: prevents force-pushing to matching branches.

## How to enable (one-time)

1. Open your repo on GitHub: **Settings** → **Rules** → **Rulesets** (under “Code and automation”).
2. Click **New ruleset** → **Import a ruleset** (or **New branch ruleset** then import).
3. **Use one of these (do not use the normal GitHub file URL):**
   - **Option A (recommended):** Open [the raw JSON file](https://raw.githubusercontent.com/kennetholsenatm-gif/LLM_Pract/main/.github/rulesets/branch-protection-security.json) in your browser, copy the entire contents, and paste into the import dialog.
   - **Option B:** If the importer accepts a URL, use this **raw** URL only (not the `github.com/.../blob/...` URL — that returns HTML and causes "Unexpected token '<', \"<!DOCTYPE\"..."). Replace `main` with your default branch if different:  
     `https://raw.githubusercontent.com/kennetholsenatm-gif/LLM_Pract/main/.github/rulesets/branch-protection-security.json`
   - **Option C:** Copy the contents of `branch-protection-security.json` from your clone and paste (or upload the file if the UI allows).
4. Review targets and rules, then create the ruleset.  
   If status checks are not yet available, run the [CI workflow](https://github.com/kennetholsenatm-gif/LLM_Pract/actions) once (e.g. **Run workflow** on the CI workflow); then the required checks will be satisfied.

After import, the rules appear under **Settings → Rules → Rulesets** and apply to pushes and pull requests targeting the default branch.

## References

- [Creating rulesets for a repository](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository)
- [Importing a ruleset](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository#importing-prebuilt-rulesets) (JSON import)
- [Available rules for rulesets](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets)
