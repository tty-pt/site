---
id: 45
title: "Phase: archive"
state: ready
severity: high
requires: [44]
validates: "pi: archive children done"
area: "phase/archive"
parent: []
---
# Phase: archive

Parents require children. This phase is done when all archive children are done. Children do not require parents.

Requires implementing phase (44). Children: `07,17,18,20,25,30,31,32,33,34,35`.
