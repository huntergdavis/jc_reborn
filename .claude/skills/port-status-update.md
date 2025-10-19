# Port Status Update Skill

Update the PS1_PORT_STATUS.md file with latest progress, achievements, and next steps.

## Usage

Run this skill after completing major milestones or at the end of a work session to document progress.

## Task

1. Read current PS1_PORT_STATUS.md
2. Identify what was accomplished in current session
3. Update completion percentages:
   - Documentation: 100%
   - Build system: X%
   - Graphics layer: X%
   - Input layer: X%
   - Audio layer: X%
   - CD-ROM I/O: X%
   - Testing: X%

4. Add new "Latest Updates" section with:
   - Date/session number
   - List of accomplishments (✅ checkmarks)
   - Major milestones achieved (🎯 emoji)
   - Files compiled and their sizes
   - Commits made

5. Update "Next Steps" section with:
   - Immediate next tasks
   - Known blockers
   - Testing plans

6. Update commit IDs at bottom

## Content Guidelines

- Use concise bullet points
- Include specific technical details (file sizes, line counts, error counts)
- Highlight major milestones with emojis
- Keep "Latest Updates" to most recent 2-3 sessions
- Archive older updates to maintain readability

## Example Update Format

```markdown
**Latest Updates** (Session N - [Brief Title]):
- ✅ Accomplished task 1 (specific details)
- ✅ Fixed compilation errors in X files
- ✅ All platform files compile with ZERO WARNINGS:
  - file1.o (XXkB)
  - file2.o (XXkB)
- 🎯 **Major Milestone**: [Achievement description]

**Next Steps**:
1. Task 1 (priority: high/medium/low)
2. Task 2
3. Task 3
```

## Success Criteria

- Status file updated with accurate information
- Percentages reflect actual completion
- Next steps are clear and actionable
- File committed to git
