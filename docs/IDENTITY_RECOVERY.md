# Identity recovery

Before libzt initialization, `identity_store.h` validates strict ZeroTier text
format, node address/public key correspondence, and both DH/signing private
key halves. A valid current secret is authoritative and repairs public data.
Otherwise a verified backup is required; a surviving address or valid public
key that conflicts with the backup fails closed. When all recognizable identity
information is lost, the verified module-local backup is the recovery authority.
Only absence of both current files, backup records and transaction permits new
identity generation. Invalid/unreadable records are not treated as absence.

Recovery commits a 0600 `state/identity.restore` record before replacing either
file. It contains the full secret (which already includes the public identity).
Each file is written through a same-directory temporary file, fsynced, renamed,
and the directory fsynced. The transaction is removed only after both files
match; interrupted recovery resumes before libzt can start. This is recoverable
two-file publication, not a claim that two filesystem renames are atomic.

New backups use `identity-backup/identity.pair`, a single 0600 secret record
containing all public/private information. The legacy two-file backup remains
readable and is preserved during migration. Current files must validate and
match before backup publication; a valid backup from another node is never
overwritten. Never expose these files in UI or logs.

Tests: `tests/test_identity_store.sh` injects failures before/after transaction
and file writes with synthetic validators; `tests/test_identity_crypto.sh`
builds host libzt and tests newly generated disposable real keys, corruption of
both private halves, and identity-preserving restoration. Neither uses device
identity files or starts a ZeroTier node.
