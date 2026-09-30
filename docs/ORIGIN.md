# Repository origin

Living Room Simulator was developed as a standalone application and imported into
[CNA Lab](https://github.com/libcna/cna-lab) as the `living-room-simulator/` subtree.
The import commit was `89f6dac1`, and the original application tip was
`590c2508d1295c66adbb7834e6d0c26c91788a3b` (Add repeatable packaging for web demo).

On 2026-09-28 the application was extracted back into its own repository using:

```bash
# Run in a CNA Lab checkout containing the historical snapshot.
git subtree split --prefix=living-room-simulator a71b942e7824f6493d65adaa1c1be912daf509c0
```

The result is the same original tip, `590c2508d1295c66adbb7834e6d0c26c91788a3b`.
All 54 commits reachable from that tip retain their original commit IDs, authors,
dates and messages. The extracted root tree is exactly the former CNA Lab subtree,
`08f05edca6f2fc650f9f3ee4310e5508c1c5f6cb`; no application code was changed during
extraction. Subsequent standalone documentation changes are separate commits.

The local branch at extraction was `develop` and the upstream is
https://github.com/libcna/living-room-simulator. The original build used sibling
`cna/`, `sharp-runtime/`, `easy-gl/` and `meta-gl/` checkouts. The maintained
bootstrap now puts these isolated checkouts under `.deps/` instead. CNA Lab no
longer carries a second working copy.

The repository is intended as a stable reference application for CNA integration
and visual checks. The existing EasyGL baseline and pinned dependency revisions
are the starting point; compatibility with current CNA revisions and other renderer,
platform and OS combinations must be verified before those combinations are claimed
as supported.
