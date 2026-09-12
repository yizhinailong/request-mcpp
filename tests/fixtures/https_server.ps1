param([Parameter(Mandatory = $true)][string]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'https_server.cs')
[McrHttpsFixture]::Run($Directory).GetAwaiter().GetResult()
