Option Explicit
' Launch create_assembly_ui.py with engine Tools\python only (never system Python).
' Supports installer layout (pythonw.exe) and venv (Scripts\pythonw.exe).
' WindowStyle MUST be 1+ — style 0 hides the Tk UI as well.

Dim fso, sh, tools, root, pyw, script, rc
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(tools)
pyw = ResolveLocalPythonw(tools)
script = tools & "\create_assembly_ui.py"

If pyw = "" Then
	MsgBox "Local Python not found." & vbCrLf & vbCrLf & _
		"Run Setup.bat first in:" & vbCrLf & root, 16, "Maho"
	WScript.Quit 1
End If

If Not fso.FileExists(script) Then
	MsgBox "Missing script:" & vbCrLf & script, 16, "Maho"
	WScript.Quit 1
End If

rc = sh.Run("""" & pyw & """ """ & script & """", 1, False)
WScript.Quit 0

Function ResolveLocalPythonw(toolsDir)
	Dim c
	c = toolsDir & "\python\pythonw.exe"
	If fso.FileExists(c) Then
		ResolveLocalPythonw = c
		Exit Function
	End If
	c = toolsDir & "\python\Scripts\pythonw.exe"
	If fso.FileExists(c) Then
		ResolveLocalPythonw = c
		Exit Function
	End If
	c = toolsDir & "\python\python.exe"
	If fso.FileExists(c) Then
		ResolveLocalPythonw = c
		Exit Function
	End If
	c = toolsDir & "\python\Scripts\python.exe"
	If fso.FileExists(c) Then
		ResolveLocalPythonw = c
		Exit Function
	End If
	ResolveLocalPythonw = ""
End Function
