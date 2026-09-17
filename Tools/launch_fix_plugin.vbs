Option Explicit
' Double-click .cplugin -> validate + auto-fix that plugin.
' Registered open handler uses wscript.exe (Windows will not quietly default to .bat).
' This .vbs does ONE thing: launch the console .bat (visible window). The .bat owns
' the fix log and its final pause, so the result stays readable.

Dim fso, sh, tools, bat, arg, cmdline
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
bat = tools & "\fix_plugin.bat"

If Not fso.FileExists(bat) Then
	MsgBox "Missing:" & vbCrLf & bat, 16, "Maho"
	WScript.Quit 1
End If

If WScript.Arguments.Count < 1 Then
	MsgBox "Usage: double-click a .cplugin file", 16, "Maho"
	WScript.Quit 1
End If

arg = WScript.Arguments(0)
' WindowStyle 1 = visible console (fix log + the .bat's own pause).
cmdline = "cmd.exe /c ""call """ & bat & """ """ & arg & """"
sh.Run cmdline, 1, False
WScript.Quit 0
