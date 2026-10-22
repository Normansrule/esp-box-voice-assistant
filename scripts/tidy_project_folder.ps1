<#
.SYNOPSIS
    Strips the folder down to just the Desk Buddy firmware.

.DESCRIPTION
    This folder started life as a clone of Espressif's esp-box repository, which
    carries a dozen other examples, hardware design files, documentation and CI
    configuration — none of which this project uses.

    This script moves all of that into a single folder called _archive, leaving
    behind a clean, standalone ESP-IDF project:

        CMakeLists.txt        the project
        partitions.csv        the flash layout
        sdkconfig.defaults    the build configuration
        README.md             the documentation
        main/                 the firmware, one folder per layer
        components/bsp/       the board support package (still needed)
        spiffs/               chimes and your music
        scripts/              these helper scripts

    NOTHING IS DELETED. Everything ends up in _archive, so you can look through
    it and copy anything back. Delete _archive yourself once you are happy.

    Run it from the project folder:

        powershell -ExecutionPolicy Bypass -File scripts\tidy_project_folder.ps1

    Add -WhatIf to see what it would do without touching anything.
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param()

$ErrorActionPreference = 'Stop'

# The project root is the folder above this script's own folder.
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$archive     = Join-Path $projectRoot '_archive'

Write-Host ''
Write-Host 'Desk Buddy — tidying the project folder' -ForegroundColor Cyan
Write-Host "  Project:  $projectRoot"
Write-Host "  Archive:  $archive"
Write-Host ''

# --------------------------------------------------------------------------
# Everything that belongs to the upstream esp-box repository rather than to
# this firmware. Each of these is moved into _archive, keeping its name.
# --------------------------------------------------------------------------
$itemsToArchive = @(
    # The other example projects, including the original factory demo this
    # firmware replaced and the ChatGPT demo it borrowed its cloud flow from.
    'examples',

    # Upstream documentation, hardware design files and prebuilt binaries.
    'docs',
    'hardware',
    'bin',

    # Upstream continuous integration and repository housekeeping.
    '.github',
    '.gitlab',
    '.gitlab-ci.yml',
    '.gitmodules',
    '.pre-commit-config.yaml',
    'codespell-ignore-list',
    'chatgpt_demo_filelist.txt',

    # Upstream helper scripts. This project's own scripts live in scripts\.
    'tools',

    # Upstream readme and its Chinese translation, replaced by our own README.
    'README_cn.md',

    # The delivery archive, now that the files are unpacked in place.
    'desk_buddy_firmware.zip',

    # Not part of the ESP-Box repository at all — see the note printed below.
    'cs162-workspace'
)

$movedCount   = 0
$skippedCount = 0
$movedUnrelated = $false

foreach ($relativePath in $itemsToArchive) {
    $source = Join-Path $projectRoot $relativePath

    if (-not (Test-Path -LiteralPath $source)) {
        Write-Host "  skip     $relativePath" -ForegroundColor DarkGray
        $skippedCount++
        continue
    }

    $destination       = Join-Path $archive $relativePath
    $destinationParent = Split-Path -Parent $destination

    if ($PSCmdlet.ShouldProcess($relativePath, 'move to _archive')) {
        if (-not (Test-Path -LiteralPath $destinationParent)) {
            New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
        }

        # -Force so a second run over a partially archived folder still works.
        Move-Item -LiteralPath $source -Destination $destination -Force
    }

    Write-Host "  archived $relativePath" -ForegroundColor Yellow
    $movedCount++

    if ($relativePath -eq 'cs162-workspace') {
        $movedUnrelated = $true
    }
}

# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------
Write-Host ''
Write-Host "Archived $movedCount item(s), skipped $skippedCount already gone." -ForegroundColor Green
Write-Host ''

if ($movedUnrelated) {
    Write-Host 'NOTE: cs162-workspace was in here and is not part of the ESP-Box' -ForegroundColor Magenta
    Write-Host '      project. It looks like coursework that ended up in the wrong' -ForegroundColor Magenta
    Write-Host '      place. It is safe in _archive\cs162-workspace — move it back' -ForegroundColor Magenta
    Write-Host '      to your Desktop if you still want it.' -ForegroundColor Magenta
    Write-Host ''
}

Write-Host 'What is left:' -ForegroundColor Cyan
Get-ChildItem -LiteralPath $projectRoot -Force |
    Where-Object { $_.Name -ne '_archive' -and $_.Name -ne '.git' } |
    Sort-Object Name |
    ForEach-Object { Write-Host ("  " + $_.Name) }

Write-Host ''
Write-Host 'Next, from an ESP-IDF terminal in this folder:' -ForegroundColor Cyan
Write-Host '    idf.py set-target esp32s3'
Write-Host '    idf.py build flash monitor'
Write-Host ''
Write-Host "Once the firmware runs, delete $archive." -ForegroundColor Cyan
Write-Host ''
