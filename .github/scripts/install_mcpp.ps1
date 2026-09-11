# Install the same mcpp release used for local validation into the runner's temporary directory.
$ErrorActionPreference = 'Stop'
$mcppVersion = '2026.9.11.1'
$mcppSha256 = 'ca46b1dadf674b4e8aa183f84832631119d36eae6807f1996b9f940c4c890ad0'
$archiveName = "mcpp-$mcppVersion-windows-x86_64"
$archivePath = Join-Path $env:RUNNER_TEMP "$archiveName.zip"
$installRoot = Join-Path $env:RUNNER_TEMP $archiveName

Invoke-WebRequest -Uri "https://github.com/mcpp-community/mcpp/releases/download/v$mcppVersion/$archiveName.zip" -OutFile $archivePath
if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $mcppSha256) {
    throw 'The mcpp release archive checksum does not match.'
}
Expand-Archive -LiteralPath $archivePath -DestinationPath $env:RUNNER_TEMP -Force

$mcppBin = Join-Path $installRoot 'bin'
$mcppExe = Join-Path $mcppBin 'mcpp.exe'
if (!(Test-Path -LiteralPath $mcppExe)) {
    throw 'The mcpp release archive does not contain bin/mcpp.exe.'
}
# The release includes registry/bin/xlings.exe; keep it in this isolated MCPP_HOME.
$env:MCPP_HOME = $installRoot
$env:PATH = "$mcppBin;$env:PATH"
"MCPP_HOME=$installRoot" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
$mcppBin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
& $mcppExe --version
if ($LASTEXITCODE -ne 0) {
    throw "mcpp --version failed with exit code $LASTEXITCODE."
}

