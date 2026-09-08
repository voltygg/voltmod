# Commits

The message is one sentence: `<type>[!]: <summary>`. No body, no trailers.

```
refactor!: rename Subscriptions to SubscriptionScope
```

- Imperative, lowercase, no period, under 72 chars. `!` when consumers must change something; that commit may add one body line saying what.
- Types: feat, fix, refactor, chore, docs, ci, style, test.
- Stage by file name. No amend, force-push, or `--no-verify` unless asked.
- Never tag on your own; a tag is a release.
