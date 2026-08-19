Option Explicit
' Right-click .cproject → Maho → 更新项目文档 → regenerate docs for the project's Source/.
' Registered as a shell sub-command under the MahoTools catalog.

Dim fso, sh, tools, root, py, script, rc
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(tools)

' Find the engine-local python (installer or venv layout), else system python.
py = ""
If fso.FileExists(tools & "\python\python.exe") Then
	py = tools & "\python\python.exe"
ElseIf fso.FileExists(tools & "\python\Scripts\python.exe") Then
	py = tools & "\python\Scripts\python.exe"
Else
	py = "python"
End If

script = tools & "\update_docs.py"

If Not fso.FileExists(script) Then
	MsgBox "Missing script:" & vbCrLf & script, 16, "Maho"
	WScript.Quit 1
End If

If WScript.Arguments.Count < 1 Then
	MsgBox "Usage: right-click a .cproject → Maho → 更新项目文档", 16, "Maho"
	WScript.Quit 1
End If

' Keep a console so the output is visible.
Dim cmdline
cmdline = "cmd.exe /c ""call """ & py & """ """ & script & """ """ & WScript.Arguments(0) & """ & pause"""
sh.Run cmdline, 1, False
WScript.Quit 0
