# Builds and packages the mod, then makes the release zip. Nothing is installed into the game.
#
# Progressive Hover Pack is an optional dependency, so the project builds without it.
# The release zip gets the source .uplugin. Always install and upload the release zip, never the one in
# Saved\ArchivedPlugins.
param(
	[ValidateSet('Editor', 'Package', 'Zip', 'All')] [string]$Step = 'All',
	[string]$Project = 'C:\SML',
	[string]$Engine = 'C:\Program Files\Unreal Engine - CSS\Engine',
	[string]$OutDir
)
$ErrorActionPreference = 'Stop'
$mod = 'HoverPackTuner'
$source = Split-Path $PSScriptRoot -Parent
$copy = Join-Path $Project "Mods\$mod"
$uproject = Join-Path $Project 'FactoryGame.uproject'
if (-not $OutDir) { $OutDir = Join-Path (Split-Path $source -Parent) "$mod-release" }

function Assert-NoOtherBuild {
	$busy = Get-CimInstance Win32_Process | Where-Object {
		$_.Name -match '^(UnrealBuildTool|AutomationTool)' -or
		($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'AutomationTool|UnrealBuildTool')
	}
	if ($busy) { throw 'Another build is running. Try again when it has finished.' }
}

if ($Step -in 'Editor', 'Package', 'All') {
	Assert-NoOtherBuild
	robocopy $source $copy /MIR /XD .git Binaries Intermediate Saved Tools /NFL /NDL /NJH /NJS | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }
}

if ($Step -in 'Editor', 'All') {
	& "$Engine\Build\BatchFiles\Build.bat" FactoryEditor Win64 Development "-project=$uproject"
	if ($LASTEXITCODE -ne 0) { throw "Editor build failed: $LASTEXITCODE" }
}

if ($Step -in 'Package', 'All') {
	Assert-NoOtherBuild
	& "$Engine\Build\BatchFiles\RunUAT.bat" "-ScriptsForProject=$uproject" PackagePlugin "-project=$uproject" '-clientconfig=Shipping' '-serverconfig=Shipping' "-DLCName=$mod" '-build' '-platform=Win64' '-nocompileeditor' '-installed'
	if ($LASTEXITCODE -ne 0) { throw "Packaging failed: $LASTEXITCODE" }
}

if ($Step -in 'Zip', 'All') {
	Add-Type -AssemblyName System.IO.Compression
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$packaged = Join-Path $Project "Saved\ArchivedPlugins\$mod\$mod-Windows.zip"
	$descriptor = Join-Path $source "$mod.uplugin"
	$info = [IO.File]::ReadAllText($descriptor) | ConvertFrom-Json
	if ($info.Version -ne [int]($info.SemVersion.Split('.')[0])) { throw 'uplugin Version must equal the SemVer major' }
	New-Item -ItemType Directory -Force $OutDir | Out-Null
	$out = Join-Path $OutDir "$mod-$($info.SemVersion).zip"
	$reader = [IO.Compression.ZipFile]::OpenRead($packaged)
	$stream = [IO.File]::Open($out, [IO.FileMode]::Create)
	$zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
	try {
		foreach ($entry in $reader.Entries) {
			$name = $entry.FullName.Replace('\', '/')
			if ($name -match '\.pdb$' -or $name.EndsWith('/')) { continue }
			$new = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
			$new.LastWriteTime = $entry.LastWriteTime
			$to = $new.Open()
			$from = if ($name -eq "$mod.uplugin") { [IO.File]::OpenRead($descriptor) } else { $entry.Open() }
			$from.CopyTo($to)
			$from.Dispose(); $to.Dispose()
			'{0,10:N0}  {1}' -f $entry.Length, $name
		}
	} finally { $zip.Dispose(); $stream.Dispose(); $reader.Dispose() }
	"Release zip: $out"
}
