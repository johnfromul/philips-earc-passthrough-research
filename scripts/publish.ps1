# Optional local GitHub CLI helper. Default private; never overwrites an existing remote.
param([string]$Repository='tcl-earc-passthrough-research',[switch]$Public)
$ErrorActionPreference='Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    & git status --short
    if($LASTEXITCODE){throw 'Not a Git repository'}
    if((& git status --porcelain)){throw 'Review and commit local changes before publishing'}
    $login=& gh api user --jq .login
    if($LASTEXITCODE){throw 'Run gh auth login first'}
    $full="$login/$Repository"
    if((& git remote)){throw 'A remote already exists; inspect it before publishing'}
    & gh repo view $full --json nameWithOwner 2>$null | Out-Null
    if($LASTEXITCODE -eq 0){throw "Repository $full already exists; no changes made"}
    $visibility=if($Public){'--public'}else{'--private'}
    & gh repo create $full $visibility --source . --remote origin --push
    if($LASTEXITCODE){throw 'Repository creation/push failed; inspect GitHub and git remote -v'}
    & git push origin --tags
    if($LASTEXITCODE){throw 'Tag push failed; retry git push origin --tags'}
    Write-Host "Published: https://github.com/$full"
} finally { Pop-Location }
