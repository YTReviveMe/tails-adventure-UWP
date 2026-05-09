param(
    [string]$DepsRoot = ".\\deps"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Patch-SdlUwpProject {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SdlUwpProjectPath
    )

    if (!(Test-Path $SdlUwpProjectPath)) {
        throw "SDL UWP project not found at: $SdlUwpProjectPath"
    }

    $xml = Get-Content -Raw -Path $SdlUwpProjectPath
    $original = $xml

    # Fix bad include path in upstream project for this solution layout.
    $xml = $xml.Replace(
        '<IncludePath>$(SolutionDir)/../src;$(IncludePath)</IncludePath>',
        '<IncludePath>$(ProjectDir)\..\src;$(IncludePath)</IncludePath>'
    )

    # Normalize toolset so Release builds do not require older VS toolset installs.
    $xml = $xml.Replace('<PlatformToolset>v142</PlatformToolset>', '<PlatformToolset>v143</PlatformToolset>')

    if ($xml -ne $original) {
        Set-Content -Path $SdlUwpProjectPath -Value $xml -Encoding UTF8
        Write-Host "Patched SDL UWP project: $SdlUwpProjectPath"
    } else {
        Write-Host "SDL UWP project already patched: $SdlUwpProjectPath"
    }
}

function Patch-SdlWinRtVideoSource {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WinRtVideoPath
    )

    if (!(Test-Path $WinRtVideoPath)) {
        throw "SDL WinRT video source not found at: $WinRtVideoPath"
    }

    $text = Get-Content -Raw -Path $WinRtVideoPath
    $original = $text

    # Keep Xbox-mode build defines, but force CoreWindow init for this non-XAML app flow.
    $fromBlock = @'
#ifndef __XBOXSERIES__
    if (!WINRT_XAMLWasEnabled) {
#endif
        data->coreWindow = CoreWindow::GetForCurrentThread();
#if SDL_WINRT_USE_APPLICATIONVIEW
        data->appView = ApplicationView::GetForCurrentView();
#endif
#ifndef __XBOXSERIES__
    }
#endif
'@

    $toBlock = @'
    // For this SDL UWP app flow, we need CoreWindow even when __XBOXSERIES__
    // is defined; renderer creation fails without it.
    if (!WINRT_XAMLWasEnabled) {
        data->coreWindow = CoreWindow::GetForCurrentThread();
#if SDL_WINRT_USE_APPLICATIONVIEW
        data->appView = ApplicationView::GetForCurrentView();
#endif
    }
'@

    $text = $text.Replace($fromBlock, $toBlock)

    if ($text -ne $original) {
        Set-Content -Path $WinRtVideoPath -Value $text -Encoding UTF8
        Write-Host "Patched SDL WinRT video source: $WinRtVideoPath"
    } else {
        Write-Host "SDL WinRT video source already patched: $WinRtVideoPath"
    }
}

New-Item -ItemType Directory -Path $DepsRoot -Force | Out-Null

$depsAbs = (Resolve-Path $DepsRoot).Path

Push-Location $depsAbs
try {
    if (!(Test-Path "SDL3-uwp-dev")) {
        git clone --depth 1 --branch uwp-main https://github.com/worleydl/SDL3-uwp-dev SDL3-uwp-dev
    }

    if (!(Test-Path "uwp-dep")) {
        git clone --depth 1 --branch basic_essential https://github.com/worleydl/uwp-dep uwp-dep
    }

    $sdlRoot = Join-Path $depsAbs "SDL3-uwp-dev"
    Patch-SdlUwpProject -SdlUwpProjectPath (Join-Path $sdlRoot "VisualC-WinRT\SDL-UWP.vcxproj")
    Patch-SdlWinRtVideoSource -WinRtVideoPath (Join-Path $sdlRoot "src\video\winrt\SDL_winrtvideo.cpp")
}
finally {
    Pop-Location
}

Write-Host "Dependencies are ready in: $depsAbs"
Write-Host "Open .\\TailsUWP.sln in Visual Studio 2022 and build/deploy x64 Debug."
