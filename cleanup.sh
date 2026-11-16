#!/bin/sh
for s in $@; do
     git -C .pi/scratch/$s checkout --detach HEAD
     git -C .pi/scratch/$s reset --hard HEAD
     git -C .pi/scratch/$s clean -ffd -e node_modules -e .lease.json
     git -C .pi/scratch/$s submodule foreach --recursive "git reset --hard && git clean -ffd && git checkout --detach HEAD" 2>/dev/null || true
     cat << EOF > .pi/scratch/$s/.lease.json
   {
     "slotId": "$s",
     "status": "idle",
     "leasedToQid": null,
     "leasedAt": null,
     "pid": null,
     "branch": null,
     "baseCommit": null
   }
EOF
done
