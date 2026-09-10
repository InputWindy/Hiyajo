# Engine Content

Engine-shipped content -- the physical directory behind the `Engine` virtual root
(`/Engine/...`), resolved by the `Paths` layer at runtime.

The content browser lists container assets (`*.casset`) under this root. Everything
else (this file included) is invisible to it: the browser is an asset view, not a
folder view.

This directory is read at runtime only -- nothing here is ever written by the editor.
