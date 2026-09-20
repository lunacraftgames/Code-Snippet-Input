[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path $_ -PathType Leaf })]
    [string]$DllPath
)

$dll = (Resolve-Path -LiteralPath $DllPath).Path
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
$regsvr = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$arguments = @('/u', '/s', "`"$dll`"")
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $registration = Start-Process -FilePath $regsvr -ArgumentList $arguments -Wait -PassThru
} else {
    $registration = Start-Process -FilePath $regsvr -Verb RunAs -ArgumentList $arguments -Wait -PassThru
}
if ($registration.ExitCode -ne 0) { throw "卸载失败，regsvr32 返回退出码 $($registration.ExitCode)。" }
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'CodeSnippetInputToolbar' -ErrorAction SilentlyContinue
Write-Host '已取消注册 Code Snippet 输入法。'
