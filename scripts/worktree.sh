#!/bin/sh
# Standalone multi-agent worktree & swarm stigmergy helper (no pi-quest required).
# Subcommands:
#   sh scripts/worktree.sh status
#   sh scripts/worktree.sh init [--all] [worktree_path]
#   sh scripts/worktree.sh create [--all] <branch_name> [worktree_path]
#   sh scripts/worktree.sh remove <branch_or_path>

set -e

# Optional heavy/sibling submodules skipped by default in new worktrees
# (~5.4 GB combined); pass --all to link every submodule.
OPTIONAL_SUBMODULES="external/axil-auren external/libjarvis external/libhyle-react external/libmm external/pi-instruct"

is_optional_submodule() {
	case " $OPTIONAL_SUBMODULES " in
		*" $1 "*) return 0 ;;
		*) return 1 ;;
	esac
}

find_primary_root() {
	curr="${1:-$(pwd)}"
	git_common=$(git -C "$curr" rev-parse --git-common-dir 2>/dev/null || true)
	if [ -n "$git_common" ]; then
		abs_common=$(cd "$curr" && cd "$git_common" && pwd)
		dirname "$abs_common"
	else
		cd "$curr" && pwd
	fi
}

PRIMARY_ROOT=$(find_primary_root "$(pwd)")
CMD="${1:-status}"
shift 2>/dev/null || true

cmd_status() {
	echo "=== 1. Git Worktrees (Primary: $PRIMARY_ROOT) ==="
	git -C "$PRIMARY_ROOT" worktree list --porcelain | awk '
		/^worktree / { wt = substr($0, 10); branch = "(detached)"; head = "" }
		/^HEAD / { head = substr($2, 1, 7) }
		/^branch / { b = substr($0, 8); sub(/^refs\/heads\//, "", b); branch = b }
		/^$/ {
			if (wt != "") {
				printf "%s|%s|%s\n", wt, branch, head
				wt = ""
			}
		}
		END {
			if (wt != "") printf "%s|%s|%s\n", wt, branch, head
		}
	' | while IFS='|' read -r wt branch head; do
		if [ -d "$wt" ]; then
			dirty=$(git -C "$wt" status --porcelain --ignore-submodules=all 2>/dev/null | wc -l | tr -d ' ')
			sub_dirty=$(git -C "$wt" submodule status 2>/dev/null | grep -c '^+' || true)
			printf "  • %-48s [%-16s @ %s] dirty_files=%s dirty_submodules=%s\n" "$wt" "$branch" "$head" "$dirty" "$sub_dirty"
		else
			printf "  • %-48s [%-16s @ %s] (MISSING DIR)\n" "$wt" "$branch" "$head"
		fi
	done

	echo ""
	echo "=== 2. Live Swarm Attention Leases (pi-mm, 15m TTL) ==="
	LEASES_FILE="$PRIMARY_ROOT/.pi/cache/code-index/swarm-leases.json"
	if [ -f "$LEASES_FILE" ] && command -v node >/dev/null 2>&1; then
		node -e '
			const fs = require("fs");
			const file = process.argv[1];
			const now = Date.now();
			const ttl = 15 * 60 * 1000;
			try {
				const leases = JSON.parse(fs.readFileSync(file, "utf8"))
					.filter(l => l && (now - l.updatedAtMs) <= ttl)
					.sort((a, b) => b.updatedAtMs - a.updatedAtMs);
				if (leases.length === 0) {
					console.log("  (no active swarm leases in the last 15 minutes)");
				} else {
					for (const l of leases.slice(0, 12)) {
						const age = Math.max(0, Math.floor((now - l.updatedAtMs) / 1000));
						const ws = l.workspaceLabel ? ` (${l.workspaceLabel})` : "";
						const sym = l.symbolName ? ` :: ${l.symbolName}` : "";
						const span = (l.startLine && l.endLine) ? `:${l.startLine}-${l.endLine}` : "";
						const act = l.action === "mutating" ? "MUTATING  " : "inspecting";
						console.log(`  • [${l.shortId}${ws}] ${act} ${l.filePath}${span}${sym} (${age}s ago)`);
					}
				}
			} catch (e) {
				console.log("  (unable to parse swarm-leases.json)");
			}
		' "$LEASES_FILE"
	else
		echo "  (no swarm-leases.json found)"
	fi

	echo ""
	echo "=== 3. Host Shared Daemons (Global Across All Worktrees) ==="
	axil_pid=$(pgrep -f "axil.*8080" | head -n 1 || true)
	jarvis_pid=$(pgrep -x "jarvisd" | head -n 1 || true)
	auren_pid=$(pgrep -f "axil-auren" | head -n 1 || true)
	if [ -n "$axil_pid" ]; then axil_st="RUNNING (PID $axil_pid)"; else axil_st="stopped"; fi
	if [ -n "$jarvis_pid" ]; then jarvis_st="RUNNING (PID $jarvis_pid)"; else jarvis_st="stopped"; fi
	if [ -n "$auren_pid" ]; then auren_st="RUNNING (PID $auren_pid)"; else auren_st="stopped"; fi
	printf "  • axil (:8080)       : %s\n" "$axil_st"
	printf "  • jarvisd (systemd)  : %s\n" "$jarvis_st"
	printf "  • axil-auren (:4242) : %s\n" "$auren_st"
}

cmd_init() {
	INCLUDE_ALL=0
	while [ "$#" -gt 0 ]; do
		case "$1" in
			--all) INCLUDE_ALL=1; shift ;;
			*) break ;;
		esac
	done

	WT_PATH="${1:-$(pwd)}"
	WT_PATH=$(cd "$WT_PATH" && pwd)
	if [ "$WT_PATH" = "$PRIMARY_ROOT" ]; then
		echo "[worktree] $WT_PATH is the primary repository root; running make stage-assets..."
		make -C "$PRIMARY_ROOT" stage-assets
		return 0
	fi

	tier_label="core"
	[ "$INCLUDE_ALL" -eq 1 ] && tier_label="all"
	echo "[worktree] Initializing worktree at $WT_PATH from primary $PRIMARY_ROOT (tier=$tier_label)..."
	linked=0
	skipped=0
	while read -r line; do
		raw_sha=$(echo "$line" | awk '{print $1}' | sed 's/^[+-U]//')
		sub_path=$(echo "$line" | awk '{print $2}')
		[ -z "$sub_path" ] && continue

		if [ "$INCLUDE_ALL" -eq 0 ] && is_optional_submodule "$sub_path"; then
			skipped=$((skipped + 1))
			continue
		fi

		primary_sub="$PRIMARY_ROOT/$sub_path"
		wt_sub="$WT_PATH/$sub_path"

		if [ -e "$primary_sub/.git" ] && [ ! -e "$wt_sub/.git" ]; then
			rm -rf "$wt_sub" 2>/dev/null || true
			git -C "$primary_sub" worktree prune --expire=now >/dev/null 2>&1 || true
			if git -C "$primary_sub" -c protocol.allow=file worktree add --detach -f "$wt_sub" "$raw_sha" >/dev/null 2>&1 || \
				git -C "$primary_sub" -c protocol.allow=file worktree add --detach -f "$wt_sub" HEAD >/dev/null 2>&1; then
				linked=$((linked + 1))
			fi
		fi
	done <<EOF
$(git -C "$PRIMARY_ROOT" submodule status)
EOF

	# Provision node_modules symlinks and exclude node_modules from git status in the worktree
	if [ -d "$PRIMARY_ROOT/node_modules" ]; then
		mkdir -p "$WT_PATH/node_modules"
		for entry in "$PRIMARY_ROOT/node_modules"/*; do
			[ -e "$entry" ] || continue
			bname=$(basename "$entry")
			[ "$bname" = "@tty-pt" ] || [ "$bname" = "@ttypt" ] && continue
			if [ ! -e "$WT_PATH/node_modules/$bname" ]; then
				ln -s "$entry" "$WT_PATH/node_modules/$bname" 2>/dev/null || true
			fi
		done
	fi

	make -C "$WT_PATH" stage-assets >/dev/null 2>&1 || true

	# Ensure .pi resources exist and share primary .pi/mm and .pi/cache
	mkdir -p "$WT_PATH/.pi"
	for item in settings.json skills prompts extensions APPEND_SYSTEM.md lsp.json; do
		if [ -e "$PRIMARY_ROOT/.pi/$item" ] && [ ! -e "$WT_PATH/.pi/$item" ]; then
			cp -R "$PRIMARY_ROOT/.pi/$item" "$WT_PATH/.pi/$item" 2>/dev/null || true
		fi
	done
	ln -sfn "$PRIMARY_ROOT/.pi/mm" "$WT_PATH/.pi/mm"
	ln -sfn "$PRIMARY_ROOT/.pi/cache" "$WT_PATH/.pi/cache"

	# Exclude node_modules from worktree git status so subagent({ worktree: true }) sees a clean checkout
	wt_git_dir=$(git -C "$WT_PATH" rev-parse --git-dir 2>/dev/null || true)
	if [ -n "$wt_git_dir" ] && [ -f "$wt_git_dir/info/exclude" ]; then
		if ! grep -qx "node_modules" "$wt_git_dir/info/exclude" 2>/dev/null; then
			echo "node_modules" >> "$wt_git_dir/info/exclude"
		fi
	fi

	echo "[worktree] Ready: $WT_PATH (tier=$tier_label, linked=$linked submodules, skipped=$skipped optional; pass --all to include optional siblings)."
}

cmd_create() {
	INCLUDE_ALL_FLAG=""
	while [ "$#" -gt 0 ]; do
		case "$1" in
			--all) INCLUDE_ALL_FLAG="--all"; shift ;;
			*) break ;;
		esac
	done

	BRANCH="$1"
	if [ -z "$BRANCH" ]; then
		echo "Usage: sh scripts/worktree.sh create [--all] <branch_name> [worktree_path]" >&2
		exit 1
	fi
	SAFE_NAME=$(echo "$BRANCH" | tr '/' '-')
	WT_PATH="${2:-$PRIMARY_ROOT/.worktrees/$SAFE_NAME}"
	mkdir -p "$(dirname "$WT_PATH")"
	git -C "$PRIMARY_ROOT" worktree prune --expire=now >/dev/null 2>&1 || true

	if git -C "$PRIMARY_ROOT" rev-parse --verify "refs/heads/$BRANCH" >/dev/null 2>&1; then
		git -C "$PRIMARY_ROOT" worktree add -f "$WT_PATH" "$BRANCH"
	else
		git -C "$PRIMARY_ROOT" worktree add -f -b "$BRANCH" "$WT_PATH" HEAD
	fi

	if [ -n "$INCLUDE_ALL_FLAG" ]; then
		cmd_init "$INCLUDE_ALL_FLAG" "$WT_PATH"
	else
		cmd_init "$WT_PATH"
	fi
}

cmd_remove() {
	TARGET="$1"
	if [ -z "$TARGET" ]; then
		echo "Usage: sh scripts/worktree.sh remove <branch_or_path>" >&2
		exit 1
	fi
	if [ -d "$TARGET" ]; then
		WT_PATH=$(cd "$TARGET" && pwd)
	elif [ -d "$PRIMARY_ROOT/.worktrees/$TARGET" ]; then
		WT_PATH="$PRIMARY_ROOT/.worktrees/$TARGET"
	elif [ -d "$PRIMARY_ROOT/.pi/scratch/$TARGET" ]; then
		WT_PATH="$PRIMARY_ROOT/.pi/scratch/$TARGET"
	else
		echo "[worktree] Worktree not found: $TARGET" >&2
		exit 1
	fi

	if [ "$WT_PATH" = "$PRIMARY_ROOT" ]; then
		echo "[worktree] Refusing to remove primary repository root!" >&2
		exit 1
	fi

	echo "[worktree] Removing worktree $WT_PATH and pruning submodule worktrees..."
	rm -rf "$WT_PATH"
	git -C "$PRIMARY_ROOT" worktree prune --expire=now >/dev/null 2>&1 || true
	git -C "$PRIMARY_ROOT" submodule foreach --quiet 'git worktree prune --expire=now >/dev/null 2>&1 || true' || true
	echo "[worktree] Removed $WT_PATH cleanly."
}

case "$CMD" in
	status) cmd_status "$@" ;;
	init)   cmd_init "$@" ;;
	create) cmd_create "$@" ;;
	remove) cmd_remove "$@" ;;
	*)
		echo "Usage: sh scripts/worktree.sh {status|init|create|remove}" >&2
		exit 1
		;;
esac
