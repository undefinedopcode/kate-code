**Here's a summary you can save to your kate-code project:**

---

## Kate-Code Project Context Summary

### Project Overview
**kate-code** is a Kate editor plugin that integrates Claude AI for code assistance. It's a fork/improvement of the original kate-code and claude-code repositories with custom enhancements.

### Current Features
- **Claude integration** — run Claude queries directly from Kate with code context
- **Claude session picker** — select which Claude session to use (currently works within the current working directory)
- **Multi-project support** — designed to work across multiple projects in a parent folder structure

### Key Technical Details

**Working Directory (cwd) Priority:**
1. Active Kate project (if shown in sidebar)
2. Active file location (if a file is open)
3. Home directory (~) as fallback

**Git Integration:**
- Projects are identified by Git repositories (`.git` folders)
- Git init is used to mark project boundaries and tie Claude sessions to specific projects
- This allows Claude to maintain context per-project across multiple sessions

### Recent Improvements & In-Progress Features

**Completed:**
- Claude session picker for selecting which conversation to continue

**In Development:**
- **CWD selector** — allow users to manually select or create the working directory for a Claude session
- **File dialog with auto git-init** — when connecting Claude code, users can create a new project folder directly; the dialog will auto-run `git init` in the new folder
- **Git repo root detection** — display the detected Git repository root to users for confirmation, so they know which project context Claude will use

### User Workflow (Target)

**Common flow (creating new project):**
1. User clicks "Connect Claude Code" button
2. File dialog opens
3. User creates new folder (or selects existing)
4. Plugin auto-runs `git init` if folder is new
5. Claude session is created/loaded for that project's cwd
6. Context is preserved across sessions because each project has its own `.git`

**Quick flow (returning user):**
1. Click "Connect Claude Code"
2. Defaults to previous cwd
3. Instantly connects to last used project

**Design principle:** Minimize friction for new projects; defer to previous context for returning users.

### Architecture Notes
- Multi-project folder structure supported (e.g., `~/projects/` containing multiple subprojects)
- Each project can sync to separate GitHub repos if desired
- Sessions are tied to Git repo roots, not individual files
- Git initialization happens automatically during project creation in the UI

### Next Steps (Post-Friday)
- Implement file dialog with new folder creation
- Add auto git-init functionality
- Add Git repo root detection and display
- Test workflow with multiple projects
- Consider session-switching capability mid-connection

---

That should get Claude up to speed on the architecture, your workflow priorities, and what you're building. Feel free to expand any section as you make progress!
