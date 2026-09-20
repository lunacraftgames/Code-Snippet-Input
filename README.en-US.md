# Code Snippet Input

**Documentation:** [简体中文](README.zh-CN.md) | [English](README.en-US.md)

Code Snippet Input is a Windows text input tool for code snippets. It appears in the Windows input-method list and displays matching snippets in a vertical candidate window near the text cursor while you type.

## Features

- Switch to it with `Win + Space`, just like another Windows input method.
- Show matching snippets in a vertical candidate list as an abbreviation is typed.
- Provide a draggable, fixed toolbar for changing the active Context and opening the manager.
- Create, rename, enable, disable, and delete Contexts and templates.
- Import and export the application's JSON configuration.
- Import IntelliJ IDEA Live Templates XML files or ZIP archives.
- Export one XML file per Context inside a ZIP archive.
- Switch the manager and toolbar UI between Simplified Chinese and English without restarting.
- Keep templates and settings in the current Windows user's local profile.

## System requirements

- 64-bit Windows 10 or Windows 11.
- Administrator permission is required when installing or uninstalling the input method.
- The portable package includes the required .NET and Visual C++ runtime components.

## Installation

1. Extract `CodeSnippetInput-Portable-win-x64.zip` completely.
2. Move the extracted folder to a permanent location.
3. Double-click `install-input-method.bat`.
4. Select **Yes** in the Windows administrator confirmation dialog.
5. Press `Win + Space` and select **Code Snippet 输入法**.

Do not run the installer from the ZIP preview. Windows records the absolute path of the input-method DLL, so do not move or rename the extracted folder after installation. To relocate it, uninstall first, move the folder, and install it again.

Restart Windows once if the input method does not immediately appear in the list.

## Opening the manager

Use any of these methods:

- Switch to Code Snippet Input and click **Manager** on the fixed toolbar.
- Double-click `open-manager.bat` in the installation folder.
- Run `publish\manager-context\CodeSnippetInput.exe` directly.

## Changing the UI language

Open the manager and use the **UI language** selector in the upper-right corner. The manager and the fixed toolbar update immediately. The selected language is saved for the current Windows user and is restored at the next launch.

The first launch follows the Windows display language: Chinese Windows uses Simplified Chinese; other languages use English.

## Using snippet candidates

1. Press `Win + Space` and switch to Code Snippet Input.
2. Type the beginning of a template abbreviation in any text input area.
3. A vertical candidate list appears near the text cursor.
4. Select a candidate to replace the typed abbreviation with the template body.

| Input | Action |
| --- | --- |
| `Up` / `Down` | Move the selection |
| `1`–`8` | Select a candidate by number |
| `Space` / `Enter` / `Tab` | Insert the selected candidate |
| Left mouse button | Click a candidate to insert it |
| `Esc` | Close the candidate list |

Up to eight candidates are shown. The list is filtered by the Context selected on the fixed toolbar. **All Contexts** searches all enabled templates.

## Fixed toolbar

The fixed toolbar is visible only while Code Snippet Input is active. It hides automatically when another input method is selected.

- Drag the logo to move the toolbar.
- Use the Context list to select the active template group.
- Click **Manager** to open the template manager.
- The close button exits the toolbar. It starts again at the next Windows sign-in or when the input method is reinstalled.

## Managing Contexts and templates

The tree on the left side of the manager shows every Context and its templates.

- Create, rename, enable, disable, or delete a Context.
- Deleting a non-empty Context also deletes its templates after confirmation.
- Create, edit, enable, disable, and delete templates.
- Click **Save** after editing. The manager also attempts to save when it closes.

Each template has a Context, abbreviation, description, body, enabled state, variable metadata, and applicability metadata.

### Literal `$variables$`

The template body is inserted exactly as written. Text enclosed in `$` characters is not evaluated or replaced, and both `$` characters are preserved.

For example:

```text
const $NAME$ = "$VALUE$";
$END$
```

is inserted exactly as shown. This differs from IntelliJ IDEA Live Templates. Values such as `$END$`, `$DATE$`, `$TIME$`, and `$CLIPBOARD$` remain literal.

## Import and export

The manager can:

- Import or export the application's JSON configuration.
- Import one Live Templates XML file.
- Import a ZIP archive containing multiple XML files.
- Export a ZIP archive containing one XML file per Context.

After importing from another tool, verify the Context, duplicate abbreviations, line endings, indentation, and any tool-specific variable expressions. Recognized XML metadata is preserved, but IntelliJ-specific code analysis, macros, automatic imports, and reformatting are not executed.

## Configuration location

Per-user data is stored in:

```text
%APPDATA%\CodeSnippetInput
```

Important files:

- `templates.json`: templates and Contexts used by the manager.
- `templates.tsf`: bridge configuration read by the input-method DLL.
- `active-context.txt`: the currently selected Context.
- `ui-language.txt`: the UI language used by the manager and toolbar.

The portable package does not contain personal configuration. Use JSON or ZIP export to create backups.

## Updating

1. Save and close the manager.
2. Close the fixed toolbar.
3. Extract the new version to a permanent folder.
4. Run the new `install-input-method.bat`.
5. Restart Windows so applications release the old input-method DLL.

## Uninstallation

1. Double-click `uninstall-input-method.bat`.
2. Select **Yes** in the administrator confirmation dialog.
3. Restart Windows.
4. Delete the installation folder if it is no longer needed.

Uninstallation does not delete `%APPDATA%\CodeSnippetInput`. Back up and remove that folder manually only if the templates and settings are no longer needed.

## Troubleshooting

### The input method is missing from `Win + Space`

Confirm that installation completed successfully and restart Windows. If the installation folder was moved, run its installer again.

### No candidate window appears while typing

Check that Code Snippet Input is active, the template is saved and enabled, the selected Context contains the template, the typed prefix matches the abbreviation, and the target application is 64-bit.

### “DLL in use” or “Access denied”

Browsers, File Explorer, and editors can keep the input-method DLL loaded. Restart Windows before replacing or deleting it.

### Is “global expansion” required?

No. Normal use through the Windows input-method system does not require it. Global expansion is a separate keyboard-hook mode and enabling both can cause duplicate expansion.

## Privacy and network use

The desktop application runs locally, requires no account, and does not upload templates. Template bodies and settings remain in the current Windows user's profile.
