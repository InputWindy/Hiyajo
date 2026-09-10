# Game Content

Project-shipped content -- the physical directory behind the `Game` virtual root
(`/Game/...`), resolved by the `Paths` layer at runtime.

Drop `*.casset` container assets in here (or in subdirectories) to have them show up
in the content browser under `/Game/...`. Non-`casset` files are not listed.

This directory is read at runtime only -- the editor never writes into it. On package
it is staged next to the executable as `Content/Game`.
