Option Explicit
' Explorer context menu "Maho -> 选择链接引擎…" (and .cproject switch) -> switch_engine.bat.
' This .vbs does ONE thing: launch the .bat (hidden). The .bat forwards to
' maho_pythonw.bat -> switch_engine.py; the Tk dialog is the visible result.

Dim fso, sh, tools, bat, arg, cmdline
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
bat = tools & "\switch_engine.bat"

If Not fso.FileExists(bat) Then
	MsgBox "Missing:" & vbCrLf & bat, 16, "Maho"
	WScript.Quit 1
End If

If WScript.Arguments.Count < 1 Then
	MsgBox "Usage: right-click a .cproject → 选择链接引擎", 16, "Maho"
	WScript.Quit 1
End If

arg = WScript.Arguments(0)
' WindowStyle 0 = hidden console (no flash); the GUI is a separate pythonw process.
cmdline = "cmd.exe /c ""call """ & bat & """ """ & arg & """"
sh.Run cmdline, 0, False
WScript.Quit 0
