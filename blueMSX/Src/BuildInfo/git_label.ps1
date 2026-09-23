# Prints the branch and commit the tree is built from, for the About dialog:
# main-1a2b3c4, with -dirty for uncommitted changes to tracked files, or unknown outside git.
Set-Location $PSScriptRoot
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { 'unknown'; exit 0 }
$env:GIT_OPTIONAL_LOCKS = '0'
$sha = git rev-parse --short=7 HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or -not $sha) { 'unknown'; exit 0 }
$branch = git symbolic-ref -q HEAD 2>$null
$branch = if ($branch) { $branch -creplace '^refs/heads/', '' } else { 'detached' }
$label = "$branch-$sha"
# The Win32 build number step rewrites these two on every build.
if (git status --porcelain --untracked-files=no -- ':/' ':!build_number.h' ':!build_info.txt' 2>$null) { $label += '-dirty' }
$label -creplace '[^A-Za-z0-9._/-]', '-'
exit 0
