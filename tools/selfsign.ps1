# Signs files with a self-signed "Styrant Audio" code-signing certificate (CI, while Azure signing
# isn't set up). The certificate is made on the build machine for this run and never leaves it; the
# private key isn't stored anywhere. It shows "Styrant Audio" in a file's Properties > Digital
# Signatures tab, but Windows doesn't trust it (SmartScreen and UAC still say "Unknown publisher").
# usage: pwsh tools/selfsign.ps1 <file> [<file> ...]
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files)
$ErrorActionPreference = 'Stop'
$subject = 'CN=Styrant Audio, O=Styrant Audio'

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object Subject -eq $subject | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -FriendlyName 'Styrant Audio' `
        -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(10)
}

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
    Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1
if (-not $signtool) { throw 'signtool.exe not found' }

foreach ($f in $Files) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing: $f" }
    # timestamped, so the signature stays valid after the certificate expires; without one if the server is down
    & $signtool.FullName sign /sha1 $cert.Thumbprint /s My /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /d 'Ultimate Audio Viewer' $f
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "timestamp server failed; signing $f without a timestamp"
        & $signtool.FullName sign /sha1 $cert.Thumbprint /s My /fd SHA256 /d 'Ultimate Audio Viewer' $f
        if ($LASTEXITCODE -ne 0) { throw "signing failed: $f" }
    }
    $sig = Get-AuthenticodeSignature -LiteralPath $f
    if ($sig.SignerCertificate.Subject -ne $subject) { throw "no Styrant Audio signature on $f" }
    Write-Host "signed (Styrant Audio): $f"
}
