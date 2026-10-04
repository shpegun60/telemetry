# clangd AST check

`check_clangd_ast.py` runs an independent clangd process against a supplied
compilation database. It sends the Qt Creator request sequence that exposed
the `Model.hpp` editor failure: initialize, open the current UTF-8 file, hover
at line 119, request a small AST range, and request the whole-file AST.
Both AST responses must be objects, and normal shutdown must exit with zero.
The opened header must produce a diagnostic report without errors.
The check leaves the running editor and its unsaved buffers alone.

```powershell
python tests/editor/check_clangd_ast.py `
  --clangd C:/Users/admin/AppData/Local/clangd/tools/clangd_21.1.8/bin/clangd.exe `
  --compile-commands-dir build/Desktop_Qt_6_10_1_MinGW_64_bit_Debug/.qtc_clangd `
  --out build/clangd-diagnosis/clangd21
```

Resolve the executable and compilation database for the machine. The paths
above describe the local reproduction environment. Use `--clangd-arg=OPTION`
to append launch options, for example `--clangd-arg=--use-dirty-headers`.
`--profile=qtcreator` selects the complete Qt Creator launch options observed
on this machine, including background indexing and dirty headers. For that
profile, copy `compile_commands.json` into a separate output directory and
pass the copy's directory, so background indexing keeps the editor's existing
cache intact. The command arguments inside the copied database stay unchanged.
`--navigation-check` also requests hover and definition on `TypeId` in the
header, requiring both to resolve before shutdown.
All replies, the server stderr, executable/source/database hashes, request
parameters and timings are written under the required `--out` directory.
The script creates no source files or project settings.

A successful run establishes that this request sequence works for the saved
file and compilation database. It does not establish how an existing editor
session handles different unsaved text or a stale compilation database.

## Selecting a tested clangd in Qt Creator

In Qt Creator, open **Preferences > C++ > Clangd**, keep **Use clangd**
selected, set **Path to executable** to the tested executable, and select
**Apply**. The local tested executable is:

```text
C:/Users/admin/AppData/Local/clangd/tools/clangd_21.1.8/bin/clangd.exe
```

Qt Creator owns this preference and the active code-model clients. Use its
preferences dialog while the editor is running, so its in-memory setting and
the persisted value agree. The setting is `[ClangdSettings]` / `ClangdPath`
in `%APPDATA%/QtProject/QtCreator.ini`. The project currently inherits these
global settings. To revert, select the bundled executable again:
`C:/Qt/Tools/QtCreator/bin/clang/bin/clangd.exe`.

The executable selection is documented in the
[Qt Creator Clangd preferences](https://doc.qt.io/qtcreator/creator-preferences-cpp-clangd.html).
The setting key and custom-path precedence are in
[Qt Creator 20.0.1 clangdsettings.cpp](https://github.com/qt-creator/qt-creator/blob/v20.0.1/src/plugins/cppeditor/clangdsettings.cpp).
