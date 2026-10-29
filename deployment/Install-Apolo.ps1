#Requires -RunAsAdministrator
# Instalacao piloto local do Apolo. Nao altera inicializacao do Windows ou UAC.
param(
    [Parameter(Mandatory = $true)]
    [string] $ExecutablePath,
    [string] $PolicyPath = (Join-Path $PSScriptRoot 'policy.ini'),
    [switch] $ReplacePolicy
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "Executavel nao encontrado: $ExecutablePath"
}
if ([IO.Path]::GetFileName($ExecutablePath) -ine 'Apolo.exe') {
    throw 'Selecione Apolo.exe gerado na compilacao Release x64.'
}
if (-not (Test-Path -LiteralPath $PolicyPath -PathType Leaf)) {
    throw "Arquivo policy.ini nao encontrado: $PolicyPath"
}
$appDirectory = Join-Path $env:ProgramFiles 'Apolo'
$policyDirectory = Join-Path $env:ProgramData 'Apolo'
New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $policyDirectory -Force | Out-Null
Copy-Item -LiteralPath $ExecutablePath -Destination (Join-Path $appDirectory 'Apolo.exe') -Force
$destinationPolicy = Join-Path $policyDirectory 'policy.ini'
if ((Test-Path -LiteralPath $destinationPolicy) -and -not $ReplacePolicy) {
    Write-Warning 'Politica existente preservada. Use -ReplacePolicy para substitui-la.'
} else {
    Copy-Item -LiteralPath $PolicyPath -Destination $destinationPolicy -Force
}
Write-Host "Executavel instalado em $appDirectory" -ForegroundColor Green
Write-Host "Politica instalada em $destinationPolicy"
Write-Host 'Antes da distribuicao: revisar ACLs, antivirus e assinatura digital.'
