param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$AsioSdkPath,
    [string]$OutputPath,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $repositoryRoot 'lib'
$jucePath = Join-Path $dependencyRoot 'juce'
$vst3Path = Join-Path $dependencyRoot 'vst3sdk'
$juceRevision = '680d75807accebce075850cbdd302ccf6c56e8f2'
$vst3Revision = '82380a8faa33824123104e005bcffb0451151d88'

if ($AsioSdkPath) {
    $asioSdkCandidates = @((Resolve-Path -LiteralPath $AsioSdkPath -ErrorAction Stop).Path)
}
else {
    $asioSdkCandidates = @(
        $env:ASIO_SDK_PATH,
        (Join-Path $repositoryRoot 'lib\ASIOSDK'),
        'C:\Dev\SDKs\ASIOSDK'
    ) | Where-Object { $_ }
}

$asioSdkRoot = $asioSdkCandidates |
    Where-Object { Test-Path (Join-Path $_ 'common\iasiodrv.h') -PathType Leaf } |
    Select-Object -First 1
if ($AsioSdkPath -and -not $asioSdkRoot) {
    throw "ASIO SDK header not found at '$AsioSdkPath\common\iasiodrv.h'."
}
if ($asioSdkRoot) {
    $asioCommonPath = Join-Path $asioSdkRoot 'common'
    Write-Host "ASIO enabled using SDK: $asioSdkRoot"
}
else {
    $asioCommonPath = $null
    Write-Warning 'ASIO SDK not found; building without ASIO. Set ASIO_SDK_PATH or pass -AsioSdkPath to enable it.'
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$FilePath' failed with exit code $LASTEXITCODE."
    }
}

function Ensure-Dependency {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Revision
    )

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
        Invoke-CheckedCommand -FilePath 'git' -Arguments @('clone', '--no-checkout', $Repository, $Path)
        Invoke-CheckedCommand -FilePath 'git' -Arguments @('-C', $Path, 'checkout', '--detach', $Revision)
        if ($Path -eq $vst3Path) {
            Invoke-CheckedCommand -FilePath 'git' -Arguments @('-C', $Path, 'submodule', 'update', '--init', '--recursive')
        }
    }

    if (-not (Test-Path (Join-Path $Path '.git'))) {
        throw "Dependency directory '$Path' exists but is not a Git checkout. Move it aside and rerun this script."
    }

    $actualRevision = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read the Git revision for '$Path'."
    }
    if ($actualRevision -ne $Revision) {
        throw "Dependency '$Path' is at revision $actualRevision; expected $Revision. Move it aside and rerun this script."
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git for Windows is required to fetch the pinned build dependencies.'
}

Ensure-Dependency -Path $jucePath -Repository 'https://github.com/juce-framework/JUCE.git' -Revision $juceRevision
Ensure-Dependency -Path $vst3Path -Repository 'https://github.com/steinbergmedia/vst3sdk.git' -Revision $vst3Revision

# Apply the ASIO recovery fix before Projucer copies JUCE modules into the app.
$asioPatch = Join-Path $repositoryRoot 'patches\juce-asio-reset.patch'
$asioSource = Join-Path $jucePath 'modules\juce_audio_devices\native\juce_win32_ASIO.cpp'
if (-not ([System.IO.File]::ReadAllText($asioSource)).Contains('Keep the first recovery deadline')) {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--check', '--ignore-space-change', $asioPatch
    )
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--ignore-space-change', $asioPatch
    )
}
else {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--reverse', '--check', '--ignore-space-change', $asioPatch
    )
}

# JUCE 4 hardcodes menu border padding; reserve the rounded surface's shadow margin.
$popupPatch = Join-Path $repositoryRoot 'patches\juce-popup-surface.patch'
$popupSource = Join-Path $jucePath 'modules\juce_gui_basics\menus\juce_PopupMenu.cpp'
if (-not ([System.IO.File]::ReadAllText($popupSource)).Contains('Light Host paints rounded shadows')) {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--check', '--ignore-space-change', $popupPatch
    )
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--ignore-space-change', $popupPatch
    )
}
else {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--reverse', '--check', '--ignore-space-change', $popupPatch
    )
}

# Keep actionable popup rows consistent with the host's clickable controls.
$popupCursorPatch = Join-Path $repositoryRoot 'patches\juce-popup-cursor.patch'
if (-not ([System.IO.File]::ReadAllText($popupSource)).Contains('Light Host uses a hand only')) {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--check', '--ignore-space-change', $popupCursorPatch
    )
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--ignore-space-change', $popupCursorPatch
    )
}
else {
    Invoke-CheckedCommand -FilePath 'git' -Arguments @(
        '-C', $jucePath, 'apply', '--reverse', '--check', '--ignore-space-change', $popupCursorPatch
    )
}

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswherePath)) {
    throw 'Visual Studio 2022 with the Desktop development with C++ workload is required.'
}

$msbuildPath = & $vswherePath -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
    Select-Object -First 1
if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    throw 'Could not find MSBuild. Install Visual Studio 2022 with the Desktop development with C++ workload.'
}

$cmakePath = & $vswherePath -latest -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' |
    Select-Object -First 1
if (-not $cmakePath -or -not (Test-Path $cmakePath)) {
    throw 'Could not find CMake. Install the C++ CMake tools component in Visual Studio Installer.'
}

$vst3BuildPath = Join-Path $repositoryRoot 'Builds\VST3SDK'
Invoke-CheckedCommand -FilePath $cmakePath -Arguments @(
    '-S', $vst3Path,
    '-B', $vst3BuildPath,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    '-DSMTG_ADD_VSTGUI=OFF',
    '-DSMTG_ADD_VST3_PLUGINS_SAMPLES=OFF',
    '-DSMTG_ADD_VST3_HOSTING_SAMPLES=OFF',
    '-DSMTG_CREATE_VST3_LINK=OFF',
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
)

foreach ($sdkProjectRelativePath in @('base\base.vcxproj', 'public.sdk\sdk.vcxproj')) {
    $sdkProjectPath = Join-Path $vst3BuildPath $sdkProjectRelativePath
    [xml]$sdkProject = [System.IO.File]::ReadAllText($sdkProjectPath)
    $sdkNamespaceManager = [System.Xml.XmlNamespaceManager]::new($sdkProject.NameTable)
    $sdkNamespaceManager.AddNamespace('msbuild', $sdkProject.DocumentElement.NamespaceURI)
    foreach ($itemDefinition in $sdkProject.SelectNodes('//msbuild:ItemDefinitionGroup', $sdkNamespaceManager)) {
        $compilerSettings = $itemDefinition.SelectSingleNode('msbuild:ClCompile', $sdkNamespaceManager)
        if ($null -eq $compilerSettings) {
            continue
        }

        $runtimeLibrary = $compilerSettings.SelectSingleNode('msbuild:RuntimeLibrary', $sdkNamespaceManager)
        if ($null -eq $runtimeLibrary) {
            continue
        }
        if ($itemDefinition.GetAttribute('Condition').Contains('Debug|')) {
            $runtimeLibrary.InnerText = 'MultiThreadedDebug'
        }
        else {
            $runtimeLibrary.InnerText = 'MultiThreaded'
        }
    }

    if ($sdkProjectRelativePath -eq 'public.sdk\sdk.vcxproj') {
        $memoryStreamSource = Join-Path $vst3Path 'public.sdk\source\common\memorystream.cpp'
        $existingCompileItems = $sdkProject.SelectNodes('//msbuild:ItemGroup/msbuild:ClCompile', $sdkNamespaceManager)
        $hasMemoryStream = $false
        foreach ($compileItem in $existingCompileItems) {
            if ($compileItem.GetAttribute('Include') -eq $memoryStreamSource) {
                $hasMemoryStream = $true
                break
            }
        }

        if (-not $hasMemoryStream) {
            $compileItemGroup = $sdkProject.SelectSingleNode('//msbuild:ItemGroup[msbuild:ClCompile]', $sdkNamespaceManager)
            if ($null -eq $compileItemGroup) {
                throw "Could not find a compile item group in '$sdkProjectPath'."
            }
            $compileItem = $sdkProject.CreateElement('ClCompile', $sdkProject.DocumentElement.NamespaceURI)
            $compileItem.SetAttribute('Include', $memoryStreamSource)
            $compileItemGroup.AppendChild($compileItem) | Out-Null
        }
    }

    $sdkProject.Save($sdkProjectPath)
}

Invoke-CheckedCommand -FilePath $cmakePath -Arguments @(
    '--build', $vst3BuildPath, '--config', $Configuration, '--target', 'base'
)
Invoke-CheckedCommand -FilePath $cmakePath -Arguments @(
    '--build', $vst3BuildPath, '--config', $Configuration, '--target', 'sdk'
)

$projucerSolution = Join-Path $jucePath 'extras\Projucer\Builds\VisualStudio2015\Projucer.sln'
Invoke-CheckedCommand -FilePath $msbuildPath -Arguments @(
    $projucerSolution,
    '-m',
    '-p:Configuration=Release',
    '-p:Platform=x64',
    '-p:PlatformToolset=v143',
    '-p:WindowsTargetPlatformVersion=10.0'
)

$projucer = Get-ChildItem (Join-Path $jucePath 'extras\Projucer\Builds\VisualStudio2015') `
    -Filter 'Projucer.exe' -File -Recurse |
    Where-Object { $_.FullName -match '\\x64\\Release\\' } |
    Select-Object -First 1
if (-not $projucer) {
    throw 'The Projucer executable was not produced.'
}

$requiredVst3Headers = @(
    'base/source/fstring.h',
    'pluginterfaces/base/conststringtable.h',
    'pluginterfaces/base/funknown.h',
    'pluginterfaces/base/ipluginbase.h',
    'pluginterfaces/base/ustring.h',
    'pluginterfaces/gui/iplugview.h',
    'pluginterfaces/vst/ivstmidicontrollers.h',
    'public.sdk/source/common/memorystream.h',
    'public.sdk/source/vst/vsteditcontroller.h'
)
foreach ($header in $requiredVst3Headers) {
    $headerPath = Join-Path $vst3Path ($header -replace '/', '\')
    if (-not (Test-Path $headerPath -PathType Leaf)) {
        throw "The VST3 SDK is missing required header '$headerPath'."
    }
}

$projectPath = Join-Path $repositoryRoot 'LightHost.jucer'
$originalProject = [System.IO.File]::ReadAllText($projectPath)
try {
    $modulePathPattern = '(<MODULEPATH\b[^>]*\bpath=")lib[\\/]+juce(")'
    $modulePathCount = [regex]::Matches($originalProject, $modulePathPattern).Count
    if ($modulePathCount -eq 0) {
        throw 'Could not find the JUCE module paths in LightHost.jucer.'
    }

    $projectXml = [regex]::Replace($originalProject, $modulePathPattern, '$1lib/juce/modules$2')
    if ($projectXml -notmatch 'JUCE_ASIO="(?:enabled|disabled)"') {
        throw 'Could not find the expected JUCE_ASIO setting.'
    }
    $asioSetting = if ($asioSdkRoot) { 'enabled' } else { 'disabled' }
    $projectXml = [regex]::Replace($projectXml, 'JUCE_ASIO="(?:enabled|disabled)"', "JUCE_ASIO=`"$asioSetting`"")

    [xml]$validatedProject = $projectXml
    foreach ($modulePath in $validatedProject.SelectNodes('//MODULEPATH')) {
        $moduleDirectory = Join-Path $repositoryRoot ($modulePath.GetAttribute('path') -replace '/', '\')
        $moduleId = $modulePath.GetAttribute('id')
        $moduleHeader = Join-Path $moduleDirectory "$moduleId\$moduleId.h"
        if (-not (Test-Path $moduleHeader -PathType Leaf)) {
            throw "JUCE module '$moduleId' is missing its header at '$moduleHeader'. Check the JUCE checkout and module paths."
        }
    }

    $visualStudioProjectDirectory = Join-Path $repositoryRoot 'Builds\VisualStudio2015'
    New-Item -ItemType Directory -Path $visualStudioProjectDirectory -Force | Out-Null

    [System.IO.File]::WriteAllText($projectPath, $projectXml, [System.Text.UTF8Encoding]::new($false))
    # Projucer is a Windows GUI executable; explicitly wait for all generated files.
    $projucerStdout = Join-Path $repositoryRoot 'Builds\projucer-stdout.log'
    $projucerStderr = Join-Path $repositoryRoot 'Builds\projucer-stderr.log'
    $projucerProcess = Start-Process -FilePath $projucer.FullName `
        -ArgumentList @('--resave', ('"' + $projectPath + '"')) `
        -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $projucerStdout -RedirectStandardError $projucerStderr
    $projucerOutput = @(Get-Content -LiteralPath $projucerStdout) +
        @(Get-Content -LiteralPath $projucerStderr)
    $projucerOutput | ForEach-Object { Write-Host $_ }
    if ($projucerProcess.ExitCode -ne 0) {
        throw "Projucer failed with exit code $($projucerProcess.ExitCode)."
    }
    if (($projucerOutput | Out-String) -match 'Error when saving:') {
        throw "Projucer could not save the project. Verify that every enabled JUCE module path points to '$jucePath\modules'."
    }

    $juceVst3HeaderPath = Join-Path $jucePath 'modules\juce_audio_processors\format_types\juce_VST3Headers.h'
    if (-not (Test-Path $juceVst3HeaderPath -PathType Leaf)) {
        throw "Projucer did not generate '$juceVst3HeaderPath'."
    }

    $juceVst3Header = [System.IO.File]::ReadAllText($juceVst3HeaderPath)
    if (-not $juceVst3Header.Contains('#define kString90S kString50_4S')) {
        $compatibilityAnchor = '(?m)(#include <public\.sdk/source/vst/vsteditcontroller\.h>\r?\n)(#else)'
        if ([regex]::Matches($juceVst3Header, $compatibilityAnchor).Count -ne 1) {
            throw 'Could not locate the JUCE VST3 headers-only compatibility insertion point.'
        }

        $compatibilityDefinitions = @(
            ' #ifndef kStringBFormat1stOrderS',
            '  #define kStringBFormat1stOrderS kStringAmbi1stOrderS',
            ' #endif',
            ' #ifndef kString90S',
            '  #define kString90S kString50_4S',
            ' #endif',
            ' #ifndef kString91S',
            '  #define kString91S kString51_4S',
            ' #endif',
            ' #ifndef kString90',
            '  #define kString90 kString50_4',
            ' #endif',
            ' #ifndef kString91',
            '  #define kString91 kString51_4',
            ' #endif'
        ) -join "`r`n"
        $juceVst3Header = [regex]::Replace(
            $juceVst3Header,
            $compatibilityAnchor,
            "`$1$compatibilityDefinitions`r`n`$2"
        )
        [System.IO.File]::WriteAllText($juceVst3HeaderPath, $juceVst3Header)
    }

    $visualStudioProjectPath = Join-Path $visualStudioProjectDirectory 'Light Host.vcxproj'
    if (-not (Test-Path $visualStudioProjectPath -PathType Leaf)) {
        throw "Projucer did not generate '$visualStudioProjectPath'."
    }

    [xml]$visualStudioProject = [System.IO.File]::ReadAllText($visualStudioProjectPath)
    $namespaceManager = [System.Xml.XmlNamespaceManager]::new($visualStudioProject.NameTable)
    $namespaceManager.AddNamespace('msbuild', $visualStudioProject.DocumentElement.NamespaceURI)
    $compileSettings = $visualStudioProject.SelectNodes(
        '//msbuild:ItemDefinitionGroup/msbuild:ClCompile', $namespaceManager)
    if ($compileSettings.Count -eq 0) {
        throw 'Could not find compiler settings in the generated Visual Studio project.'
    }

    $juceModuleIncludeDirectories = @('$(ProjectDir)..\..\lib\juce\modules')
    foreach ($modulePath in $validatedProject.SelectNodes('//MODULEPATH')) {
        $juceModuleIncludeDirectories += '$(ProjectDir)..\..\lib\juce\modules\' +
            $modulePath.GetAttribute('id')
    }
    $juceModuleIncludePrefix = ($juceModuleIncludeDirectories -join ';') + ';'

    foreach ($compileSetting in $compileSettings) {
        # These pinned JUCE/MSVC diagnostics are limited to the compiler settings for this app.
        $disabledWarnings = $compileSetting.SelectSingleNode(
            'msbuild:DisableSpecificWarnings', $namespaceManager)
        if ($null -eq $disabledWarnings) {
            $disabledWarnings = $visualStudioProject.CreateElement(
                'DisableSpecificWarnings', $visualStudioProject.DocumentElement.NamespaceURI)
            $compileSetting.AppendChild($disabledWarnings) | Out-Null
        }
        foreach ($warning in @('4723', '4840')) {
            if ($disabledWarnings.InnerText -notmatch "(^|;)$warning(;|$)") {
                $disabledWarnings.InnerText = $warning + ';' + $disabledWarnings.InnerText
            }
        }

        $includeDirectories = $compileSetting.SelectSingleNode(
            'msbuild:AdditionalIncludeDirectories', $namespaceManager)
        if ($null -eq $includeDirectories) {
            $includeDirectories = $visualStudioProject.CreateElement(
                'AdditionalIncludeDirectories', $visualStudioProject.DocumentElement.NamespaceURI)
            $compileSetting.AppendChild($includeDirectories) | Out-Null
        }
        if (-not $includeDirectories.InnerText.Contains('lib\juce\modules')) {
            $includeDirectories.InnerText = $juceModuleIncludePrefix + $includeDirectories.InnerText
        }
        if (-not $includeDirectories.InnerText.Contains('lib\vst3sdk')) {
            $includeDirectories.InnerText = '$(ProjectDir)..\..\lib\vst3sdk;' + $includeDirectories.InnerText
        }
        if ($asioCommonPath -and -not $includeDirectories.InnerText.Contains($asioCommonPath)) {
            $includeDirectories.InnerText = $asioCommonPath + ';' + $includeDirectories.InnerText
        }

        $preprocessorDefinitions = $compileSetting.SelectSingleNode(
            'msbuild:PreprocessorDefinitions', $namespaceManager)
        if ($null -eq $preprocessorDefinitions) {
            $preprocessorDefinitions = $visualStudioProject.CreateElement(
                'PreprocessorDefinitions', $visualStudioProject.DocumentElement.NamespaceURI)
            $compileSetting.AppendChild($preprocessorDefinitions) | Out-Null
        }
        if (-not $preprocessorDefinitions.InnerText.Contains('JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY')) {
            $preprocessorDefinitions.InnerText =
                'JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY=1;' + $preprocessorDefinitions.InnerText
        }
    }

    $visualStudioProject.Save($visualStudioProjectPath)

    $linkSettings = $visualStudioProject.SelectNodes(
        '//msbuild:ItemDefinitionGroup/msbuild:Link', $namespaceManager)
    foreach ($linkSetting in $linkSettings) {
        $libraryDirectories = $linkSetting.SelectSingleNode(
            'msbuild:AdditionalLibraryDirectories', $namespaceManager)
        if ($null -eq $libraryDirectories) {
            $libraryDirectories = $visualStudioProject.CreateElement(
                'AdditionalLibraryDirectories', $visualStudioProject.DocumentElement.NamespaceURI)
            $linkSetting.AppendChild($libraryDirectories) | Out-Null
        }
        if (-not $libraryDirectories.InnerText.Contains('Builds\VST3SDK\lib')) {
            $libraryDirectories.InnerText =
                '$(ProjectDir)..\..\Builds\VST3SDK\lib\$(Configuration);' + $libraryDirectories.InnerText
        }

        $dependencies = $linkSetting.SelectSingleNode(
            'msbuild:AdditionalDependencies', $namespaceManager)
        if ($null -eq $dependencies) {
            $dependencies = $visualStudioProject.CreateElement(
                'AdditionalDependencies', $visualStudioProject.DocumentElement.NamespaceURI)
            $linkSetting.AppendChild($dependencies) | Out-Null
        }
        $requiredLibraries = @(
            'sdk.lib', 'base.lib', 'uuid.lib', 'propsys.lib', 'ole32.lib',
            'oleaut32.lib', 'gdi32.lib', 'user32.lib', 'shell32.lib',
            'comdlg32.lib', 'advapi32.lib', 'winmm.lib', 'opengl32.lib',
            'ws2_32.lib', 'shlwapi.lib', 'imm32.lib', 'kernel32.lib'
        )
        foreach ($library in $requiredLibraries) {
            if (-not $dependencies.InnerText.Contains($library)) {
                $dependencies.InnerText = $library + ';' + $dependencies.InnerText
            }
        }
    }
    $visualStudioProject.Save($visualStudioProjectPath)
    [xml]$savedVisualStudioProject = [System.IO.File]::ReadAllText($visualStudioProjectPath)
    $savedNamespaceManager = [System.Xml.XmlNamespaceManager]::new(
        $savedVisualStudioProject.NameTable)
    $savedNamespaceManager.AddNamespace(
        'msbuild', $savedVisualStudioProject.DocumentElement.NamespaceURI)
    $savedWarningSettings = $savedVisualStudioProject.SelectNodes(
        '//msbuild:ItemDefinitionGroup/msbuild:ClCompile/msbuild:DisableSpecificWarnings',
        $savedNamespaceManager)
    if ($savedWarningSettings.Count -ne $compileSettings.Count) {
        throw 'Could not save the MSVC warning suppressions to the generated project.'
    }

    $outputDirectory = Join-Path $repositoryRoot "Builds\Windows\$Configuration"
    if ($OutputPath) {
        $outputDirectory = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
            [System.IO.Path]::GetFullPath($OutputPath)
        } else {
            [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputPath))
        }
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $solutionPath = Join-Path $repositoryRoot 'Builds\VisualStudio2015\Light Host.sln'
    Invoke-CheckedCommand -FilePath $msbuildPath -Arguments @(
        $solutionPath,
        '-m',
        "-p:Configuration=$Configuration",
        '-p:Platform=x64',
        '-p:PlatformToolset=v143',
        '-p:WindowsTargetPlatformVersion=10.0',
        "-p:OutDir=$outputDirectory\"
    )

    $executablePath = Join-Path $outputDirectory 'Light Host.exe'
    if (-not (Test-Path $executablePath -PathType Leaf)) {
        throw "Build completed without producing '$executablePath'."
    }

    Write-Host "Built executable: $executablePath"

    if ($RunTests) {
        # Reuse the generated compiler/linker settings, with separate test output.
        [xml]$testProject = $visualStudioProject.OuterXml
        $testNamespaces = [System.Xml.XmlNamespaceManager]::new($testProject.NameTable)
        $testNamespaces.AddNamespace('msbuild', $testProject.DocumentElement.NamespaceURI)
        foreach ($source in $testProject.SelectNodes('//msbuild:ItemGroup/msbuild:ClCompile', $testNamespaces)) {
            if ($source.GetAttribute('Include') -match '[\\/]Source[\\/]') {
                $source.ParentNode.RemoveChild($source) | Out-Null
            }
        }
        $sourceGroup = $testProject.SelectSingleNode('//msbuild:ItemGroup[msbuild:ClCompile]', $testNamespaces)
        $testSource = $testProject.CreateElement('ClCompile', $testProject.DocumentElement.NamespaceURI)
        $testSource.SetAttribute('Include', (Join-Path $repositoryRoot 'Tests\PluginChainTests.cpp'))
        $sourceGroup.AppendChild($testSource) | Out-Null
        $windowSource = $testProject.CreateElement('ClCompile', $testProject.DocumentElement.NamespaceURI)
        $windowSource.SetAttribute('Include', (Join-Path $repositoryRoot 'Source\PluginWindow.cpp'))
        $sourceGroup.AppendChild($windowSource) | Out-Null
        foreach ($link in $testProject.SelectNodes('//msbuild:ItemDefinitionGroup/msbuild:Link', $testNamespaces)) {
            $link.SelectSingleNode('msbuild:SubSystem', $testNamespaces).InnerText = 'Console'
            $link.SelectSingleNode('msbuild:OutputFile', $testNamespaces).InnerText = '$(OutDir)\PluginChainTests.exe'
        }
        $testProjectPath = Join-Path $visualStudioProjectDirectory 'PluginChainTests.vcxproj'
        $testProject.Save($testProjectPath)
        $testOutput = Join-Path $repositoryRoot "Builds\Tests\$Configuration"
        Invoke-CheckedCommand -FilePath $msbuildPath -Arguments @(
            $testProjectPath, '-m', "-p:Configuration=$Configuration", '-p:Platform=x64',
            '-p:PlatformToolset=v143', '-p:WindowsTargetPlatformVersion=10.0',
            '-p:TargetName=PluginChainTests',
            "-p:OutDir=$testOutput\", "-p:IntDir=$testOutput\obj\"
        )
        Invoke-CheckedCommand -FilePath (Join-Path $testOutput 'PluginChainTests.exe') -Arguments @()
    }
}
finally {
    [System.IO.File]::WriteAllText($projectPath, $originalProject, [System.Text.UTF8Encoding]::new($false))
}
