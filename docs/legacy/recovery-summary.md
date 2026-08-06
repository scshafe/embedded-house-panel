# Legacy Recovery Summary

The 2026-08-05 recovery audit inspected all saved branches, commits, deleted
tracked files, reflogs, stashes, tags, and unreachable Git objects in the four
adjacent repositories. It found three real work streams:

| Saved repository | Durable evidence | Restart decision |
| --- | --- | --- |
| `bbb-dev-scripts` (`95bbff3`) | Debian/VM cross-development and board-copy scripts | Replace copied target sysroots with the Buildroot SDK |
| `bbb-gpio-waker` (`c52bca1`) | PIR/backlight proof-of-concept, provisional GPIO0_3 clue | Preserve the intent; replace the custom ABI and unsafe hardware assumptions |
| `question-list-manager` (`38a7b09`) | Most-developed GTK vocabulary flow, 480×272 layout, night-red intent | Preserve the interaction model; replace storage, toolkit coupling, and network code |
| `question-manager-user-app` (`ae87230`) | Snapshot of an abandoned `question-list-manager` branch | No independent implementation to merge |

There was no saved integration service, custom OS definition, complete remote
vocabulary client, or suspend/resume policy. The clones contained no additional
stashes, tags, local branches, or unreachable objects from which missing work
could be recovered. This is why the project is an informed restart rather than
a merge of the old repositories.

The full workspace-local audit remains at `../../../docs/legacy-system-audit.md`
outside this repository. This summary intentionally carries the durable facts
needed by a future clone without importing any legacy source dependency.
