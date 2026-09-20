[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path $_ -PathType Leaf })]
    [string]$DllPath
)

$dll = (Resolve-Path -LiteralPath $DllPath).Path
if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    throw '请从 64 位 PowerShell 运行此脚本以注册 x64 DLL。'
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
$regsvr = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$arguments = @('/s', "`"$dll`"")
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $registration = Start-Process -FilePath $regsvr -ArgumentList $arguments -Wait -PassThru
} else {
    $registration = Start-Process -FilePath $regsvr -Verb RunAs -ArgumentList $arguments -Wait -PassThru
}
if ($registration.ExitCode -ne 0) { throw "注册失败，regsvr32 返回退出码 $($registration.ExitCode)。" }

$clsidKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{20B13E38-A7B1-4ECF-A263-956894F41828}\InprocServer32'
$registeredDll = if (Test-Path -LiteralPath $clsidKey) { (Get-Item -LiteralPath $clsidKey).GetValue('') } else { $null }
if (-not [string]::Equals($registeredDll, $dll, [StringComparison]::OrdinalIgnoreCase)) {
    throw "注册校验失败。系统记录的 DLL 路径为：$registeredDll"
}

$publishRoot = Split-Path -Parent (Split-Path -Parent $dll)
$toolbar = Join-Path $publishRoot 'manager-context\CodeSnippetInput.exe'
if (Test-Path -LiteralPath $toolbar -PathType Leaf) {
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $runKey -Force | Out-Null
    $toolbarCommand = '"{0}" --toolbar' -f $toolbar
    Set-ItemProperty -Path $runKey -Name 'CodeSnippetInputToolbar' -Value $toolbarCommand
    Start-Process -FilePath $toolbar -ArgumentList '--toolbar'
} else {
    Write-Warning "未找到悬浮栏程序：$toolbar"
}

Write-Host '已注册 Code Snippet 输入法。若输入法列表未立即刷新，请注销后重新登录，或重启 ctfmon.exe。'
Write-Host '固定悬浮栏已启动，并已设置为登录时自动启动。'
Write-Host '之后可按 Win + 空格，在“Code Snippet 输入法”与其他输入法之间切换。'
