Option Explicit
' Double-click .cproject → generate sibling .sln (console via cmd).
' Registered open handler uses wscript.exe (Windows will not quietly default to .bat).

Dim fso, sh, tools, root, bat, arg, cmdline
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(tools)
bat = tools & "\generateProject.bat"

If Not fso.FileExists(bat) Then
	MsgBox "Missing:" & vbCrLf & bat, 16, "Maho"
	WScript.Quit 1
End If

If WScript.Arguments.Count < 1 Then
	MsgBox "Usage: double-click a .cproject file", 16, "Maho"
	WScript.Quit 1
End If

arg = WScript.Arguments(0)
' Keep a console so cmake / generate logs are visible; pause only on failure.
cmdline = "cmd.exe /c ""call """ & bat & """ """ & arg & """ & if errorlevel 1 (echo. & pause)"""
sh.Run cmdline, 1, False
WScript.Quit 0
