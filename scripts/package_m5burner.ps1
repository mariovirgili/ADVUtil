param(
    [string]$EnvName = "m5stack-stamps3",
    [string]$Version = "0.4.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot ".pio\build\$EnvName"
$distDir = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distDir "m5burner\ADVUtil"
$firmwareDir = Join-Path $packageRoot "firmware"
$fullFlashBin = Join-Path $distDir ("ADVUtil_v{0}_fullflash_0x0.bin" -f $Version)
$zipPath = Join-Path $distDir ("ADVUtil_m5burner_v{0}.zip" -f $Version)

$esptool = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\esptool.exe"
$bootApp0 = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

if (!(Test-Path $buildDir)) {
    throw "Build directory not found: $buildDir"
}
if (!(Test-Path $esptool)) {
    throw "esptool not found: $esptool"
}
if (!(Test-Path $bootApp0)) {
    throw "boot_app0.bin not found: $bootApp0"
}

$appBin = Get-ChildItem $buildDir -Filter *.bin |
    Where-Object { $_.Name -notin @("bootloader.bin", "partitions.bin") } |
    Sort-Object Length -Descending |
    Select-Object -First 1

if ($null -eq $appBin) {
    throw "Application binary not found in $buildDir"
}

$bootloaderBin = Join-Path $buildDir "bootloader.bin"
$partitionsBin = Join-Path $buildDir "partitions.bin"

foreach ($requiredFile in @($bootloaderBin, $partitionsBin, $appBin.FullName)) {
    if (!(Test-Path $requiredFile)) {
        throw "Required binary not found: $requiredFile"
    }
}

New-Item -ItemType Directory -Force -Path $firmwareDir | Out-Null

& $esptool --chip esp32s3 merge_bin `
    -o $fullFlashBin `
    --flash_mode dio `
    --flash_freq 80m `
    --flash_size 8MB `
    0x0000 $bootloaderBin `
    0x8000 $partitionsBin `
    0xE000 $bootApp0 `
    0x10000 $appBin.FullName

Copy-Item $bootloaderBin (Join-Path $firmwareDir "bootloader_0x0.bin") -Force
Copy-Item $partitionsBin (Join-Path $firmwareDir "partitions_0x8000.bin") -Force
Copy-Item $bootApp0 (Join-Path $firmwareDir "boot_app0_0xe000.bin") -Force
Copy-Item $appBin.FullName (Join-Path $firmwareDir ("ADVUtil_v{0}_0x10000.bin" -f $Version)) -Force

$repository = "https://github.com/mariovirgili/ADVUtil"
$manifest = @"
{
  "name": "ADVUtil",
  "description": "Air Mouse BLE + GPS utility for M5Stack Cardputer.",
  "keywords": "Cardputer, ESP32-S3, BLE, GPS, Air Mouse",
  "author": "mariovirgili",
  "repository": "$repository",
  "firmware_category": {
    "Cardputer": {
      "path": "firmware",
      "device": ["Cardputer"],
      "default_baud": 1500000
    }
  },
  "version": "$Version",
  "framework": "Arduino"
}
"@

Set-Content -Path (Join-Path $packageRoot "m5burner.json") -Value $manifest -Encoding ascii

$packageReadme = @'
# ADVUtil for M5Burner

This folder contains:

- `m5burner.json`: M5Burner manifest
- `firmware\`: split flash images for M5Burner
- `..\ADVUtil_v{0}_fullflash_0x0.bin`: merged full-flash image

Split image offsets:

- `bootloader_0x0.bin`
- `partitions_0x8000.bin`
- `boot_app0_0xe000.bin`
- `ADVUtil_v{0}_0x10000.bin`
'@ -f $Version

Set-Content -Path (Join-Path $packageRoot "README.md") -Value $packageReadme -Encoding ascii

if (Test-Path (Join-Path $repoRoot "README.md")) {
    Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $packageRoot "PROJECT_README.md") -Force
}

if (Test-Path (Join-Path $repoRoot "media\Title.jpg")) {
    Copy-Item (Join-Path $repoRoot "media\Title.jpg") (Join-Path $packageRoot "cover.jpg") -Force
}

if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Full-flash image: $fullFlashBin"
Write-Host "M5Burner package: $packageRoot"
Write-Host "M5Burner zip: $zipPath"
