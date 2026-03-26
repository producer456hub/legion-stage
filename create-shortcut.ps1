$WshShell = New-Object -ComObject WScript.Shell
$Desktop = [Environment]::GetFolderPath('Desktop')
$Shortcut = $WshShell.CreateShortcut("$Desktop\Sequencer.lnk")
$Shortcut.TargetPath = "C:\dev\sequencer\build\Sequencer_artefacts\Release\Sequencer.exe"
$Shortcut.WorkingDirectory = "C:\dev\sequencer"
$Shortcut.Description = "Launch Sequencer"
$Shortcut.Save()
Write-Host "Shortcut created on Desktop"
