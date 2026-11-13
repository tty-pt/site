#!/usr/bin/env bash
# scripts/squash-since.sh
#
# Given a reference commit in the root repository, squashes all work since
# that reference in:
#   1. Every submodule whose gitlink or HEAD moved since the reference commit
#      (plus any uncommitted tracked changes when --include-dirty / -a is used).
#   2. The root repository itself, recording the newly squashed submodule
#      gitlinks alongside all root repo changes since the reference commit.
#
# Usage:
#   scripts/squash-since.sh <ref-commit> [-m "commit message"] [-a|--include-dirty] [--dry-run]

set -euo pipefail

usage() {
	cat <<EOF
Usage: $0 <ref-commit> [-m <message>] [-a|--include-dirty] [--dry-run]

Arguments:
  <ref-commit>         Commit hash (or rev) in the main repository to squash back to.
  -m, --message <msg>  Optional custom commit message (defaults to combined log summary).
  -a, --include-dirty  Also stage and include uncommitted tracked changes (git add -u)
                       in the main repo and submodules as part of the squash.
  -n, --dry-run        Show what would be squashed without modifying any repository.
EOF
	exit 1
}

REF_COMMIT=""
CUSTOM_MSG=""
INCLUDE_DIRTY=0
DRY_RUN=0

while [ $# -gt 0 ]; do
	case "$1" in
		-m|--message)
			[ $# -ge 2 ] || usage
			CUSTOM_MSG="$2"
			shift 2
			;;
		-a|--include-dirty)
			INCLUDE_DIRTY=1
			shift
			;;
		--dry-run|-n)
			DRY_RUN=1
			shift
			;;
		-h|--help)
			usage
			;;
		-*)
			echo "Unknown option: $1" >&2
			usage
			;;
		*)
			if [ -z "$REF_COMMIT" ]; then
				REF_COMMIT="$1"
			else
				echo "Unexpected argument: $1" >&2
				usage
			fi
			shift
			;;
	esac
done

[ -n "$REF_COMMIT" ] || usage

ROOT_DIR="$(git rev-parse --show-toplevel)"
cd "$ROOT_DIR"

# Verify that REF_COMMIT exists and is an ancestor of HEAD in the root repo
if ! git rev-parse --verify "${REF_COMMIT}^{commit}" >/dev/null 2>&1; then
	echo "Error: '$REF_COMMIT' is not a valid commit in the main repository." >&2
	exit 1
fi

REF_HASH="$(git rev-parse "${REF_COMMIT}^{commit}")"
HEAD_HASH="$(git rev-parse HEAD)"

if ! git merge-base --is-ancestor "$REF_HASH" "$HEAD_HASH"; then
	echo "Error: '$REF_COMMIT' ($REF_HASH) is not an ancestor of HEAD ($HEAD_HASH)." >&2
	exit 1
fi

# When not using --include-dirty, ensure the root index has no pre-staged changes
# that would accidentally get folded into the squashed root commit.
# (Unstaged working-tree edits and untracked files are untouched by git reset --soft + git commit.)
if [ "$INCLUDE_DIRTY" -eq 0 ] && ! git diff --cached --ignore-submodules --quiet; then
	echo "Error: Main repository has staged changes in the index. Commit/unstage them or pass --include-dirty (-a)." >&2
	exit 1
fi

echo "Reference commit: $REF_HASH ($(git log -1 --format='%s' "$REF_HASH"))"
echo "Current HEAD:     $HEAD_HASH ($(git log -1 --format='%s' "$HEAD_HASH"))"
echo ""

# Collect submodule paths from .gitmodules (if present)
SUBMODULE_PATHS=()
if [ -f .gitmodules ]; then
	mapfile -t SUBMODULE_PATHS < <(git config --file .gitmodules --get-regexp '^submodule\..*\.path$' 2>/dev/null | awk '{print $2}' || true)
fi

# Phase 1: Preflight validation across all submodules before mutating any state
declare -a ACTION_SUB_PATHS=()
declare -a ACTION_SUB_BASES=()
declare -a ACTION_SUB_HEADS=()
declare -a ACTION_SUB_COUNTS=()
declare -a ACTION_SUB_DIRTY=()
declare -a ACTION_SUB_TYPES=() # "squash" | "reset-empty" | "keep-single"

WILL_CHANGE_ROOT_GITLINKS=0

for sub_path in "${SUBMODULE_PATHS[@]}"; do
	[ -e "$sub_path/.git" ] || continue

	# Guard against corrupted submodule core.worktree pointing to a scratch slot
	sub_toplevel="$(git -C "$sub_path" rev-parse --show-toplevel 2>/dev/null || true)"
	if [ "$sub_toplevel" != "$ROOT_DIR/$sub_path" ]; then
		echo "Error: Submodule '$sub_path' worktree resolves to '$sub_toplevel' instead of '$ROOT_DIR/$sub_path'." >&2
		exit 1
	fi

	# Find the submodule gitlink (mode 160000) recorded at REF_HASH in the root repo,
	# or in the first commit after REF_HASH that introduced the submodule.
	sub_base_hash="$(git ls-tree "$REF_HASH" -- "$sub_path" | awk '$1 == "160000" {print $3}')"
	if [ -z "$sub_base_hash" ]; then
		intro_commit="$(git log --reverse --format='%H' "${REF_HASH}..${HEAD_HASH}" -- "$sub_path" | sed -n '1p')"
		if [ -n "$intro_commit" ]; then
			sub_base_hash="$(git ls-tree "$intro_commit" -- "$sub_path" | awk '$1 == "160000" {print $3}')"
		fi
	fi
	if [ -z "$sub_base_hash" ]; then
		echo "[skip] $sub_path: no submodule gitlink at or since reference commit ${REF_HASH:0:8}"
		continue
	fi

	sub_root_head_hash="$(git ls-tree "$HEAD_HASH" -- "$sub_path" | awk '$1 == "160000" {print $3}')"
	sub_head_hash="$(git -C "$sub_path" rev-parse HEAD)"

	# Verify sub_base_hash exists in submodule and is an ancestor of submodule's HEAD
	if ! git -C "$sub_path" cat-file -e "${sub_base_hash}^{commit}" 2>/dev/null; then
		echo "Error: Base commit $sub_base_hash not found in submodule '$sub_path'." >&2
		exit 1
	fi

	if ! git -C "$sub_path" merge-base --is-ancestor "$sub_base_hash" "$sub_head_hash"; then
		common_base="$(git -C "$sub_path" merge-base "$sub_base_hash" "$sub_head_hash" 2>/dev/null || true)"
		if [ -n "$common_base" ]; then
			echo "[notice] Submodule '$sub_path' base ${sub_base_hash:0:8} diverged from HEAD; falling back to merge-base ${common_base:0:8}"
			sub_base_hash="$common_base"
		else
			echo "Error: Submodule '$sub_path' base $sub_base_hash is not an ancestor of its checked-out HEAD $sub_head_hash." >&2
			exit 1
		fi
	fi

	# Ensure the submodule's checked-out HEAD is not behind the gitlink recorded at root HEAD
	if [ -n "$sub_root_head_hash" ] && ! git -C "$sub_path" merge-base --is-ancestor "$sub_root_head_hash" "$sub_head_hash"; then
		echo "Error: Submodule '$sub_path' checked-out HEAD (${sub_head_hash:0:8}) is behind or diverged from root HEAD's gitlink (${sub_root_head_hash:0:8}). Run 'git submodule update' first." >&2
		exit 1
	fi

	sub_has_dirty=0
	if ! git -C "$sub_path" diff --quiet || ! git -C "$sub_path" diff --cached --quiet; then
		sub_has_dirty=1
	fi

	if [ "$sub_base_hash" = "$sub_head_hash" ]; then
		if [ "$INCLUDE_DIRTY" -eq 1 ] && [ "$sub_has_dirty" -eq 1 ]; then
			ACTION_SUB_PATHS+=("$sub_path")
			ACTION_SUB_BASES+=("$sub_base_hash")
			ACTION_SUB_HEADS+=("$sub_head_hash")
			ACTION_SUB_COUNTS+=("0")
			ACTION_SUB_DIRTY+=("1")
			ACTION_SUB_TYPES+=("squash")
			WILL_CHANGE_ROOT_GITLINKS=1
		fi
		continue
	fi

	# If this submodule has commits to squash and --include-dirty is NOT set,
	# ensure it doesn't have pre-staged index changes that would get mixed into the commit.
	if [ "$INCLUDE_DIRTY" -eq 0 ] && ! git -C "$sub_path" diff --cached --quiet; then
		echo "Error: Submodule '$sub_path' has staged changes in its index. Commit/unstage them or pass --include-dirty (-a)." >&2
		exit 1
	fi

	commit_count="$(git -C "$sub_path" rev-list --count "${sub_base_hash}..${sub_head_hash}")"

	if [ "$INCLUDE_DIRTY" -eq 1 ] && [ "$sub_has_dirty" -eq 1 ]; then
		ACTION_SUB_TYPES+=("squash")
		WILL_CHANGE_ROOT_GITLINKS=1
	elif git -C "$sub_path" diff --quiet "$sub_base_hash" "$sub_head_hash"; then
		ACTION_SUB_TYPES+=("reset-empty")
		if [ "$sub_base_hash" != "$sub_root_head_hash" ]; then
			WILL_CHANGE_ROOT_GITLINKS=1
		fi
	elif [ "$commit_count" -eq 1 ] && [ -z "$CUSTOM_MSG" ]; then
		ACTION_SUB_TYPES+=("keep-single")
		if [ "$sub_head_hash" != "$sub_root_head_hash" ]; then
			WILL_CHANGE_ROOT_GITLINKS=1
		fi
	else
		ACTION_SUB_TYPES+=("squash")
		WILL_CHANGE_ROOT_GITLINKS=1
	fi

	ACTION_SUB_PATHS+=("$sub_path")
	ACTION_SUB_BASES+=("$sub_base_hash")
	ACTION_SUB_HEADS+=("$sub_head_hash")
	ACTION_SUB_COUNTS+=("$commit_count")
	ACTION_SUB_DIRTY+=("$sub_has_dirty")
done

# Helper to build a combined commit message from a revision range in a repo
build_squash_message() {
	local repo_dir="$1"
	local base_rev="$2"
	local head_rev="$3"
	local count="$4"
	local fallback_subject="$5"

	if [ -n "$CUSTOM_MSG" ]; then
		printf "%s\n" "$CUSTOM_MSG"
		return
	fi

	if [ "$count" -eq 0 ]; then
		printf "%s\n" "$fallback_subject"
		return
	fi

	if [ "$count" -eq 1 ]; then
		git -C "$repo_dir" log -1 --format='%B' "$head_rev"
		return
	fi

	# Prefer the first commit on the first-parent line (or first non-merge commit) as the top subject
	local first_hash
	first_hash="$(git -C "$repo_dir" rev-list --first-parent --no-merges --reverse "${base_rev}..${head_rev}" | sed -n '1p')"
	if [ -z "$first_hash" ]; then
		first_hash="$(git -C "$repo_dir" rev-list --no-merges --reverse "${base_rev}..${head_rev}" | sed -n '1p')"
	fi
	if [ -z "$first_hash" ]; then
		first_hash="$(git -C "$repo_dir" rev-list --reverse "${base_rev}..${head_rev}" | sed -n '1p')"
	fi

	local first_subject="$fallback_subject"
	if [ -n "$first_hash" ]; then
		first_subject="$(git -C "$repo_dir" log -1 --format='%s' "$first_hash")"
	fi

	local log_list
	log_list="$(git -C "$repo_dir" log --no-merges --reverse --format='- %s%n%w(0,2,2)%b' "${base_rev}..${head_rev}" | sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d')"

	if [ -n "$log_list" ]; then
		printf "%s\n\nSquashed commits:\n%s\n" "$first_subject" "$log_list"
	else
		printf "%s\n" "$first_subject"
	fi
}

# Helper to ensure a submodule in detached HEAD whose 'main' branch was at old_head
# keeps 'main' aligned with the squashed HEAD.
align_submodule_branch() {
	local sub_path="$1"
	local base_hash="$2"
	local old_head="$3"

	if git -C "$sub_path" symbolic-ref -q HEAD >/dev/null 2>&1; then
		return
	fi
	if git -C "$sub_path" show-ref --verify --quiet refs/heads/main; then
		if git -C "$sub_path" merge-base --is-ancestor "$base_hash" refs/heads/main &&
		   git -C "$sub_path" merge-base --is-ancestor refs/heads/main "$old_head"; then
			git -C "$sub_path" checkout -B main HEAD >/dev/null 2>&1 || true
		fi
	fi
}

# Phase 2: Execute submodule squashes
SQUASHED_SUBMODULES=()

for i in "${!ACTION_SUB_PATHS[@]}"; do
	sub_path="${ACTION_SUB_PATHS[$i]}"
	sub_base_hash="${ACTION_SUB_BASES[$i]}"
	sub_head_hash="${ACTION_SUB_HEADS[$i]}"
	commit_count="${ACTION_SUB_COUNTS[$i]}"
	sub_dirty="${ACTION_SUB_DIRTY[$i]}"
	action_type="${ACTION_SUB_TYPES[$i]}"

	dirty_note=""
	if [ "$INCLUDE_DIRTY" -eq 1 ] && [ "$sub_dirty" -eq 1 ]; then
		dirty_note=" + dirty tracked changes"
	fi
	echo "[submodule] $sub_path: $commit_count commit(s)${dirty_note} since ${sub_base_hash:0:8}"

	case "$action_type" in
		reset-empty)
			echo "  -> Tree is identical to base; resetting $sub_path back to ${sub_base_hash:0:8}"
			if [ "$DRY_RUN" -eq 0 ]; then
				git -C "$sub_path" reset --soft "$sub_base_hash"
				align_submodule_branch "$sub_path" "$sub_base_hash" "$sub_head_hash"
				SQUASHED_SUBMODULES+=("$sub_path")
			fi
			;;
		keep-single)
			echo "  -> Already a single commit (${sub_head_hash:0:8}); keeping as-is"
			if [ "$DRY_RUN" -eq 0 ]; then
				align_submodule_branch "$sub_path" "$sub_base_hash" "$sub_head_hash"
				SQUASHED_SUBMODULES+=("$sub_path")
			fi
			;;
		squash)
			sub_msg="$(build_squash_message "$sub_path" "$sub_base_hash" "$sub_head_hash" "$commit_count" "chore($sub_path): squash work since ${sub_base_hash:0:8}")"
			if [ "$DRY_RUN" -eq 1 ]; then
				echo "  -> [dry-run] Would reset --soft ${sub_base_hash:0:8} and commit in $sub_path"
			else
				if [ "$INCLUDE_DIRTY" -eq 1 ] && [ "$sub_dirty" -eq 1 ]; then
					git -C "$sub_path" add -u
				fi
				git -C "$sub_path" reset --soft "$sub_base_hash"
				if git -C "$sub_path" diff --cached --quiet; then
					echo "  -> Nothing staged after reset in $sub_path; keeping at ${sub_base_hash:0:8}"
				else
					git -C "$sub_path" commit -m "$sub_msg"
				fi
				align_submodule_branch "$sub_path" "$sub_base_hash" "$sub_head_hash"
				new_sub_hash="$(git -C "$sub_path" rev-parse HEAD)"
				echo "  -> Squashed into ${new_sub_hash:0:8}"
				SQUASHED_SUBMODULES+=("$sub_path")
			fi
			;;
	esac
done

# Phase 3: Squash the root repository back to REF_HASH
root_commit_count="$(git rev-list --count "${REF_HASH}..${HEAD_HASH}")"
root_has_dirty=0
if [ "$INCLUDE_DIRTY" -eq 1 ]; then
	if ! git diff --ignore-submodules --quiet || ! git diff --cached --ignore-submodules --quiet; then
		root_has_dirty=1
	fi
fi

root_dirty_note=""
if [ "$root_has_dirty" -eq 1 ]; then
	root_dirty_note=" + dirty tracked changes"
fi
echo ""
echo "[root] $root_commit_count commit(s)${root_dirty_note} in main repo since ${REF_HASH:0:8}"

if [ "$root_commit_count" -eq 0 ] && [ "$WILL_CHANGE_ROOT_GITLINKS" -eq 0 ] && [ "$root_has_dirty" -eq 0 ]; then
	echo "Nothing to squash in main repo or submodules since ${REF_HASH:0:8}."
	exit 0
fi

if [ "$root_commit_count" -eq 1 ] && [ -z "$CUSTOM_MSG" ] && [ "$WILL_CHANGE_ROOT_GITLINKS" -eq 0 ] && [ "$root_has_dirty" -eq 0 ]; then
	echo "  -> Main repo is already a single commit (${HEAD_HASH:0:8}) with up-to-date submodule gitlinks; keeping as-is."
	exit 0
fi

if [ "$DRY_RUN" -eq 1 ]; then
	echo "  -> [dry-run] Would reset --soft ${REF_HASH:0:8} and commit in main repo"
	exit 0
fi

if [ "$INCLUDE_DIRTY" -eq 1 ] && [ "$root_has_dirty" -eq 1 ]; then
	git add -u
fi

root_msg="$(build_squash_message "$ROOT_DIR" "$REF_HASH" "$HEAD_HASH" "$root_commit_count" "chore: squash updates since ${REF_HASH:0:8}")"

git reset --soft "$REF_HASH"

# Stage all updated/squashed submodule gitlinks in the root index
for sub_path in "${SQUASHED_SUBMODULES[@]}"; do
	git add "$sub_path"
done

if git diff --cached --quiet; then
	echo "Nothing to commit in main repo after reset to ${REF_HASH:0:8}."
else
	git commit -m "$root_msg"
	echo "  -> Main repo squashed into $(git rev-parse --short HEAD)"
fi
