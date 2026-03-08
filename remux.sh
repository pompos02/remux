#!/usr/bin/env bash

set -euo pipefail

# Preview behavior is configurable through env vars so the picker can stay fast
# on slower networks or larger SSH configs without requiring edits to the script.
PREVIEW_CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/remux-preview"
PREVIEW_CACHE_TTL="${REMUX_PREVIEW_CACHE_TTL:-30}"
PREVIEW_LOCK_TTL="${REMUX_PREVIEW_LOCK_TTL:-20}"
PREVIEW_WINDOW_SIZE_PERCENT="${REMUX_PREVIEW_WINDOW_SIZE_PERCENT:-60}"
SSH_CONFIG_PATH="${SSH_CONFIG_PATH:-~/.ssh/config}"

COLOR_DEFAULT=$'\033[0m'
COLOR_GREEN=$'\033[1;32m'
COLOR_RED=$'\033[1;31m'
COLOR_YELLOW=$'\033[1;33m'
COLOR_BLUE=$'\033[1;34m'
COLOR_ORANGE=$'\033[38;5;208m'

ENV_TYPE_EMPTY='__ENV_EMPTY__'

# Host rows are colorized by the optional `# env:` metadata found near each Host
# block in the SSH config. A dedicated sentinel lets us distinguish between
# "no env metadata was provided" and an actual empty shell value.
declare -Ar LISTING_COLOR_BY_ENV_TYPE=(
	[prod]="$COLOR_RED"
	[comp]="$COLOR_YELLOW"
	[app]="$COLOR_BLUE"
	[db]="$COLOR_ORANGE"
	["$ENV_TYPE_EMPTY"]="$COLOR_GREEN"
)

if ! [[ "$PREVIEW_CACHE_TTL" =~ ^[0-9]+$ ]]; then
	PREVIEW_CACHE_TTL=30
fi

if ! [[ "$PREVIEW_LOCK_TTL" =~ ^[0-9]+$ ]]; then
	PREVIEW_LOCK_TTL=20
fi

if ! [[ "$PREVIEW_WINDOW_SIZE_PERCENT" =~ ^[0-9]+$ ]] || ((PREVIEW_WINDOW_SIZE_PERCENT <= 0 || PREVIEW_WINDOW_SIZE_PERCENT >= 100)); then
	PREVIEW_WINDOW_SIZE_PERCENT=60
fi

# Preview output is sometimes rendered inside fzf and sometimes piped elsewhere.
# Only emit ANSI color codes when stdout is interactive or fzf explicitly tells us
# it is rendering a preview pane.
if [[ -t 1 || -n "${FZF_PREVIEW_LINES-}" ]]; then
	PREVIEW_COLOR_RESET=$'\033[0m'
	PREVIEW_COLOR_LABEL=$'\033[34m'
	PREVIEW_COLOR_POSITIVE=$'\033[32m'
	PREVIEW_COLOR_WARNING=$'\033[33m'
	PREVIEW_COLOR_NEGATIVE=$'\033[31m'
	PREVIEW_COLOR_INFO=$'\033[36m'
	PREVIEW_COLOR_DIM=$'\033[2m'
else
	PREVIEW_COLOR_RESET=''
	PREVIEW_COLOR_LABEL=''
	PREVIEW_COLOR_POSITIVE=''
	PREVIEW_COLOR_WARNING=''
	PREVIEW_COLOR_NEGATIVE=''
	PREVIEW_COLOR_INFO=''
	PREVIEW_COLOR_DIM=''
fi

ssh_config_files() {
	local config_path path

	# Resolve all SSH config files that may contribute host definitions. This keeps
	# parsing behavior aligned with common OpenSSH layouts: a main user config,
	# optional config.d fragments, and the system-wide fallback config.
	config_path="${SSH_CONFIG_PATH/#\~/$HOME}"
	[[ -f "$config_path" ]] && printf '%s\n' "$config_path"

	for path in "$HOME"/.ssh/config.d/*; do
		[[ -f "$path" ]] && printf '%s\n' "$path"
	done

	[[ -f /etc/ssh/ssh_config ]] && printf '%s\n' /etc/ssh/ssh_config
}

__fzf_list_host_metadata() {
	local -a config_files=()
	local path

	# Build a concrete list of config files first so awk receives only paths that
	# actually exist. If nothing is present we simply return no hosts.
	while IFS= read -r path; do
		[[ -n "$path" ]] && config_files+=("$path")
	done < <(ssh_config_files)

	((${#config_files[@]} > 0)) || return 0

	# Parse SSH configs and extract three columns per concrete host alias:
	#   host<TAB>env<TAB>description
	# Metadata is taken from comment lines immediately above a Host block:
	#   # env: prod
	#   # desc: Payments database
	# Wildcard/pattern hosts are ignored because they are not selectable targets.
	awk '
		function trim(s) {
			gsub(/^[ \t\r\n]+|[ \t\r\n]+$/, "", s)
			return s
		}

		function sanitize(s) {
			s = trim(s)
			gsub(/\t/, " ", s)
			return s
		}

		function reset_pending() {
			pending_env = ""
			pending_env_set = 0
			pending_desc = ""
		}

		function flush_block(i) {
			output_env = env
			if (env_set && output_env == "") {
				output_env = "__ENV_EMPTY__"
			}

			if (host_count > 0) {
				for (i = 1; i <= host_count; ++i) {
					print hosts[i] "\t" output_env "\t" desc
				}
			}

			delete hosts
			host_count = 0
			env = ""
			env_set = 0
			desc = ""
		}

		FNR == 1 {
			reset_pending()
		}

		{
			raw = $0
			trimmed = trim(raw)

			if (trimmed == "") {
				next
			}

			if (trimmed ~ /^#/) {
				comment = trimmed
				sub(/^#[ \t]*/, "", comment)

				if (tolower(comment) ~ /^desc[ \t]*:/) {
					value = comment
					sub(/^[Dd][Ee][Ss][Cc][ \t]*:[ \t]*/, "", value)
					pending_desc = sanitize(value)
				} else if (tolower(comment) ~ /^env[ \t]*:/) {
					value = comment
					sub(/^[Ee][Nn][Vv][ \t]*:[ \t]*/, "", value)
					pending_env = sanitize(value)
					pending_env_set = 1
				}

				next
			}

			line = raw
			sub(/#.*/, "", line)
			line = trim(line)
			if (line == "") {
				next
			}

			field_count = split(line, parts, /[ \t]+/)
			key = tolower(parts[1])

			if (key != "host") {
				if (host_count == 0) {
					reset_pending()
				}
				next
			}

			flush_block()
			env = pending_env
			env_set = pending_env_set
			desc = pending_desc
			reset_pending()

			for (i = 2; i <= field_count; ++i) {
				if (parts[i] !~ /[*?%!]/) {
					hosts[++host_count] = parts[i]
				}
			}
		}

		END {
			flush_block()
		}
	' "${config_files[@]}"
}

__fzf_list_hosts() {
	local host env desc

	# Compatibility helper that returns only host aliases, dropping metadata.
	while IFS=$'\t' read -r host env desc; do
		[[ -n "$host" ]] && printf '%s\n' "$host"
	done < <(__fzf_list_host_metadata)
}

resolve_host_metadata() {
	local target_host="$1"
	local host env desc

	# Find the env/description tuple for a single host. The blank-tab-blank fallback
	# preserves the caller's expected two-field read behavior even when no metadata
	# exists.
	while IFS=$'\t' read -r host env desc; do
		if [[ "$host" == "$target_host" ]]; then
			printf '%s\t%s\n' "$env" "$desc"
			return 0
		fi
	done < <(__fzf_list_host_metadata)

	printf '\t\n'
}

tmux_has_session() {
	local name="$1"
	# `tmux has-session` exits non-zero when the session is absent, which makes it a
	# clean predicate for conditionals.
	tmux has-session -t "$name" 2>/dev/null
}

activate_session() {
	local name="$1"

	if [[ -n "${TMUX:-}" ]]; then
		tmux switch-client -t "$name"
	else
		tmux attach-session -t "$name"
	fi
}

launch_default_session() {
	local alias="$1"

	# The default path is one persistent tmux session per SSH alias. Re-selecting the
	# same host reuses the existing session instead of starting a second SSH login.
	if ! tmux_has_session "$alias"; then
		tmux new-session -d -s "$alias" -c "$HOME"
		tmux send-keys -t "$alias:1.1" "ssh $alias" C-m
	fi

	activate_session "$alias"
}

launch_custom_user_session() {
	local alias="$1"
	local user="$2"
	local target="ssh ${user}@${alias}"

	# Custom-user launches are intentionally additive: if the base session already
	# exists we open a new window inside it so the original alias session remains
	# available.
	if tmux_has_session "$alias"; then
		tmux new-window -t "$alias" -c "$HOME" "$target"
	else
		tmux new-session -d -s "$alias" -c "$HOME" "$target"
	fi

	activate_session "$alias"
}

copy_hostname() {
	local hostname="$1"

	# Support the common clipboard commands across WSL, macOS, Wayland, and X11.
	# The first available command wins so callers can treat this as best effort.
	if command -v clip.exe >/dev/null 2>&1; then
		printf '%s' "$hostname" | clip.exe
		return 0
	fi

	if command -v pbcopy >/dev/null 2>&1; then
		printf '%s' "$hostname" | pbcopy
		return 0
	fi

	if command -v wl-copy >/dev/null 2>&1; then
		printf '%s' "$hostname" | wl-copy
		return 0
	fi

	if command -v xclip >/dev/null 2>&1; then
		printf '%s' "$hostname" | xclip -selection clipboard
		return 0
	fi

	if command -v xsel >/dev/null 2>&1; then
		printf '%s' "$hostname" | xsel --clipboard --input
		return 0
	fi

	printf 'No clipboard command found; cannot copy hostname\n' >&2
	return 1
}

list_host_entries() {
	# When explicit args are provided, synthesize rows in the same tab-separated
	# shape produced by metadata parsing so downstream formatting code can stay
	# uniform. Without args, enumerate every SSH host from config.
	if (($# > 0)); then
		local host
		for host in "$@"; do
			printf '%s\t\t\n' "$host"
		done
	else
		__fzf_list_host_metadata
	fi
}

list_hosts() {
	local host env desc

	while IFS=$'\t' read -r host env desc; do
		[[ -n "$host" ]] && printf '%s\n' "$host"
	done < <(list_host_entries "$@")
}

print_picker_row() {
	local host="$1"
	local is_active="$2"
	local env="${3-}"
	local hostname="${4-}"
	local max_host_width="${5:-0}"
	local display env_color marker prefix_len pad_width

	env_color="${LISTING_COLOR_BY_ENV_TYPE[$env]:-}"

	# Column 1 is a human-friendly display string for fzf, while later columns keep
	# raw machine-readable values. Padding is computed from the plain hostname length
	# so ANSI color codes do not break alignment.
	if [[ -n "$env_color" ]]; then
		display="${env_color}${host}${COLOR_DEFAULT}"
	else
		display="$host"
	fi

	marker=''
	if [[ "$is_active" == '1' ]]; then
		marker="${COLOR_GREEN}*${COLOR_DEFAULT}"
		display+="$marker"
	fi

	prefix_len=${#host}
	[[ "$is_active" == '1' ]] && ((prefix_len++))
	pad_width=$((max_host_width + 1 - prefix_len))
	if ((pad_width > 0)); then
		display+="$(printf '%*s' "$pad_width" '')"
	fi

	if [[ -n "$hostname" ]]; then
		if [[ -n "$env_color" ]]; then
			display+=" ${env_color}${hostname}${COLOR_DEFAULT}"
		else
			display+=" ${hostname}"
		fi
	fi

	printf '%s\t%s\t%s\n' "$display" "$host" "$env"
}

build_picker_rows() {
	declare -A sessions=()
	declare -A seen_hosts=()
	local -a row_hosts=()
	local -a row_envs=()
	local -a row_hostnames=()
	local -a row_actives=()
	local session host env desc active host_width max_host_width hostname _ i
	max_host_width=0

	# Collect existing tmux session names up front so active hosts can be marked in
	# the picker without repeated tmux lookups per row.
	while IFS= read -r session; do
		[[ -n "$session" ]] && sessions["$session"]=1
	done < <(tmux ls -F '#{session_name}' 2>/dev/null || true)

	# Resolve metadata and SSH-derived hostname details for each unique alias, then
	# emit all rows only after the widest host column is known.
	while IFS=$'\t' read -r host env desc; do
		[[ -z "$host" || -n "${seen_hosts[$host]+x}" ]] && continue
		seen_hosts["$host"]=1
		IFS=$'\t' read -r _ hostname < <(resolve_host_identity "$host")
		active=0
		[[ -n "${sessions[$host]+x}" ]] && active=1
		row_hosts+=("$host")
		row_envs+=("$env")
		row_hostnames+=("$hostname")
		row_actives+=("$active")
		host_width=${#host}
		if ((host_width > max_host_width)); then
			max_host_width=$host_width
		fi
	done < <(list_host_entries "$@")

	for i in "${!row_hosts[@]}"; do
		print_picker_row "${row_hosts[$i]}" "${row_actives[$i]}" "${row_envs[$i]}" "${row_hostnames[$i]}" "$max_host_width"
	done
}

pick_host() {
	local script_path="${BASH_SOURCE[0]}"

	# fzf returns two lines when `--expect` is used: the pressed key and the selected
	# row. Preview rendering shells back into this script so the preview logic stays
	# in one place rather than being duplicated in an inline command.
	build_picker_rows "$@" | fzf \
		--ansi \
		--style=full \
		--height=85% \
		--layout=reverse \
		--preview-label=' details ' \
		--prompt='> ' \
		--marker='+' \
		--info=inline-right \
		--header-first \
		--expect=enter,ctrl-x,ctrl-y \
		--delimiter=$'\t' \
		--with-nth=1 \
		--preview "bash \"$script_path\" --preview-host {2}" \
		--preview-window="right,${PREVIEW_WINDOW_SIZE_PERCENT}%,wrap"
}

preview_color_for_status() {
	case "$1" in
	reachable)
		printf '%s' "$PREVIEW_COLOR_POSITIVE"
		;;
	unreachable)
		printf '%s' "$PREVIEW_COLOR_NEGATIVE"
		;;
	loading... | unknown)
		printf '%s' "$PREVIEW_COLOR_WARNING"
		;;
	*)
		printf '%s' "$PREVIEW_COLOR_INFO"
		;;
	esac
}

preview_color_for_auth() {
	case "$1" in
	'key login works')
		printf '%s' "$PREVIEW_COLOR_POSITIVE"
		;;
	'key login failed')
		printf '%s' "$PREVIEW_COLOR_NEGATIVE"
		;;
	loading... | unknown)
		printf '%s' "$PREVIEW_COLOR_WARNING"
		;;
	*)
		printf '%s' "$PREVIEW_COLOR_INFO"
		;;
	esac
}

preview_color_for_uptime() {
	case "$1" in
	loading...)
		printf '%s' "$PREVIEW_COLOR_WARNING"
		;;
	unavailable)
		printf '%s' "$PREVIEW_COLOR_DIM"
		;;
	*)
		printf '%s' "$PREVIEW_COLOR_INFO"
		;;
	esac
}

print_preview_metric() {
	local label="$1"
	local value="$2"
	local color="$3"
	local is_cached="${4:-0}"
	local cached_suffix=''

	# Stale cached values are still useful because they avoid a blank preview while a
	# fresh background probe is running, so label them explicitly instead of hiding
	# them.
	if ((is_cached == 1)); then
		cached_suffix=" ${PREVIEW_COLOR_DIM}(cached)${PREVIEW_COLOR_RESET}"
	fi

	printf '%s%s:%s %s%s%s%s\n' \
		"$PREVIEW_COLOR_LABEL" "$label" "$PREVIEW_COLOR_RESET" \
		"$color" "$value" "$PREVIEW_COLOR_RESET" "$cached_suffix"
}

print_host_preview() {
	local host="$1"
	local user hostname env desc cache_file host_header env_label
	local has_cache=0
	local cache_is_fresh=0
	local cached_flag=0

	IFS=$'\t' read -r user hostname < <(resolve_host_identity "$host")
	IFS=$'\t' read -r env desc < <(resolve_host_metadata "$host")
	cache_file="$(preview_cache_file "$host")"

	# Preview rendering must feel instant, so it follows a stale-while-revalidate
	# pattern: show cached probe data when available, and kick off a background probe
	# whenever the cache is missing or too old.
	if load_probe_cache "$cache_file"; then
		has_cache=1
		if probe_cache_is_fresh "$cache_updated_at"; then
			cache_is_fresh=1
		fi
	fi

	if ((cache_is_fresh == 0)); then
		start_probe_async "$host" "$hostname" "$cache_file"
	fi

	host_header="$host"
	env_label="$env"
	[[ "$env_label" == "$ENV_TYPE_EMPTY" ]] && env_label=''
	if [[ -n "$env_label" ]]; then
		host_header+=" ${PREVIEW_COLOR_INFO}[${env_label}]${PREVIEW_COLOR_RESET}"
	fi

	printf '%sHost:%s %s\n' "$PREVIEW_COLOR_LABEL" "$PREVIEW_COLOR_RESET" "$host_header"
	printf '%sUser:%s %s\n' "$PREVIEW_COLOR_LABEL" "$PREVIEW_COLOR_RESET" "$user"
	printf '%sHostname:%s %s\n' "$PREVIEW_COLOR_LABEL" "$PREVIEW_COLOR_RESET" "$hostname"

	if [[ -n "$desc" ]]; then
		printf '\n%sDescription:%s %s\n' "$PREVIEW_COLOR_LABEL" "$PREVIEW_COLOR_RESET" "$desc"
	fi

	printf '\n'

	if ((has_cache == 1)); then
		if ((cache_is_fresh == 0)); then
			cached_flag=1
		fi

		print_preview_metric 'Status' "$cache_status" "$(preview_color_for_status "$cache_status")" "$cached_flag"
		print_preview_metric 'Auth' "$cache_auth" "$(preview_color_for_auth "$cache_auth")" "$cached_flag"
		print_preview_metric 'Uptime' "$cache_uptime" "$(preview_color_for_uptime "$cache_uptime")" "$cached_flag"
	else
		print_preview_metric 'Status' 'loading...' "$PREVIEW_COLOR_WARNING"
		print_preview_metric 'Auth' 'loading...' "$PREVIEW_COLOR_WARNING"
		print_preview_metric 'Uptime' 'loading...' "$PREVIEW_COLOR_WARNING"
	fi
}

cache_updated_at=''
cache_status=''
cache_auth=''
cache_uptime=''

preview_cache_file() {
	local host="$1"
	# Sanitize host aliases into filesystem-safe cache keys while keeping them
	# readable for debugging.
	local cache_key="${host//[^[:alnum:]._-]/_}"
	printf '%s/%s.cache\n' "$PREVIEW_CACHE_DIR" "$cache_key"
}

resolve_host_identity() {
	local host="$1"
	local key value _
	local user=''
	local hostname=''

	# `ssh -G` asks OpenSSH to print the fully resolved config for a host without
	# opening a connection. We read only the first `user` and `hostname` values,
	# which is enough for display and probe purposes.
	while read -r key value _; do
		case "$key" in
		user)
			if [[ -z "$user" ]]; then
				user="$value"
			fi
			;;
		hostname)
			if [[ -z "$hostname" ]]; then
				hostname="$value"
			fi
			;;
		esac

		if [[ -n "$user" && -n "$hostname" ]]; then
			break
		fi
	done < <(ssh -G "$host" 2>/dev/null || true)

	if [[ -z "$user" ]]; then
		user='unknown'
	fi

	if [[ -z "$hostname" ]]; then
		hostname="$host"
	fi

	printf '%s\t%s\n' "$user" "$hostname"
}

load_probe_cache() {
	local cache_file="$1"
	local key value

	# Reset globals first so callers never observe stale values from a previous cache
	# file after a failed read.
	cache_updated_at=''
	cache_status=''
	cache_auth=''
	cache_uptime=''

	if [[ ! -f "$cache_file" ]]; then
		return 1
	fi

	while IFS='=' read -r key value; do
		case "$key" in
		updated_at)
			cache_updated_at="$value"
			;;
		status)
			cache_status="$value"
			;;
		auth)
			cache_auth="$value"
			;;
		uptime)
			cache_uptime="$value"
			;;
		esac
	done <"$cache_file"

	[[ -n "$cache_updated_at" && -n "$cache_status" && -n "$cache_auth" && -n "$cache_uptime" ]]
}

probe_cache_is_fresh() {
	local updated_at="$1"
	local now

	# Cache freshness is tracked in epoch seconds so we can compare ages cheaply
	# without parsing locale-specific timestamps.
	if ! [[ "$updated_at" =~ ^[0-9]+$ ]]; then
		return 1
	fi

	now="$(date +%s)"
	((now - updated_at <= PREVIEW_CACHE_TTL))
}

probe_host_details() {
	local host="$1"
	local hostname="$2"
	local status auth uptime uptime_raw
	local -a ssh_auth_opts

	# Use a quick TCP probe first to distinguish "host unreachable" from
	# "reachable but SSH auth failed". The subsequent SSH command is locked to key-
	# based auth so the preview never hangs on password or keyboard-interactive
	# prompts.
	ssh_auth_opts=(
		-o BatchMode=yes
		-o PreferredAuthentications=publickey
		-o PubkeyAuthentication=yes
		-o PasswordAuthentication=no
		-o KbdInteractiveAuthentication=no
		-o ChallengeResponseAuthentication=no
		-o NumberOfPasswordPrompts=0
		-o ConnectionAttempts=1
	)

	status='unreachable'
	auth='unknown'
	uptime='unavailable'

	if nc -z -w1 "$hostname" 22 >/dev/null 2>&1; then
		status='reachable'
		auth='key login failed'
		uptime_raw="$(ssh "${ssh_auth_opts[@]}" -o ConnectTimeout=2 "$host" 'uptime' 2>/dev/null || true)"

		if [[ -n "$uptime_raw" ]]; then
			auth='key login works'
			uptime="${uptime_raw#* up }"
			uptime="${uptime%%, *user*}"
			uptime="${uptime%%, load average*}"
		fi
	fi

	printf '%s\t%s\t%s\n' "$status" "$auth" "$uptime"
}

probe_host_to_cache() {
	local host="$1"
	local hostname="$2"
	local cache_file="$3"
	local lock_dir="$4"
	local status auth uptime
	local now tmp_file

	# Write probe results to a temp file and rename into place atomically so preview
	# readers never consume a partially written cache file.
	tmp_file="${cache_file}.$$"
	trap "rm -f \"$tmp_file\" 2>/dev/null || true; rmdir \"$lock_dir\" 2>/dev/null || true" EXIT

	IFS=$'\t' read -r status auth uptime < <(probe_host_details "$host" "$hostname")
	now="$(date +%s)"

	{
		printf 'updated_at=%s\n' "$now"
		printf 'status=%s\n' "$status"
		printf 'auth=%s\n' "$auth"
		printf 'uptime=%s\n' "$uptime"
	} >"$tmp_file"

	mv "$tmp_file" "$cache_file"
}

start_probe_async() {
	local host="$1"
	local hostname="$2"
	local cache_file="$3"
	local lock_dir now lock_mtime
	local script_path="${BASH_SOURCE[0]}"

	# Background probes are coordinated with a lock directory. mkdir is atomic, so it
	# doubles as a portable lock primitive. Old locks are treated as abandoned and
	# cleaned up after a TTL to avoid permanent stuck previews.
	mkdir -p "$PREVIEW_CACHE_DIR"
	lock_dir="${cache_file}.lock"

	if [[ -d "$lock_dir" ]]; then
		now="$(date +%s)"
		lock_mtime="$(stat -c %Y "$lock_dir" 2>/dev/null || printf '0')"
		if [[ "$lock_mtime" =~ ^[0-9]+$ ]] && ((now - lock_mtime > PREVIEW_LOCK_TTL)); then
			rmdir "$lock_dir" 2>/dev/null || true
		fi
	fi

	if mkdir "$lock_dir" 2>/dev/null; then
		nohup bash "$script_path" --probe-host "$host" "$hostname" "$cache_file" "$lock_dir" >/dev/null 2>&1 &
	fi
}

# Internal subcommands let the script re-enter itself for preview rendering and
# background probing without needing helper files.
if [[ "${1-}" == '--probe-host' ]]; then
	[[ -n "${2-}" && -n "${3-}" && -n "${4-}" && -n "${5-}" ]]
	probe_host_to_cache "$2" "$3" "$4" "$5"
	exit 0
fi

if [[ "${1-}" == '--preview-host' ]]; then
	[[ -n "${2-}" ]]
	print_host_preview "$2"
	exit 0
fi

main() {
	local selection key row alias hostname

	# The main loop keeps the picker open for non-launch actions such as copying the
	# resolved hostname. It exits only after a session is launched or the picker is
	# cancelled.
	while true; do
		if ! selection="$(pick_host "$@")"; then
			exit 0
		fi

		key="${selection%%$'\n'*}"
		row="${selection#*$'\n'}"

		if [[ "$row" == "$selection" || -z "$row" ]]; then
			exit 0
		fi

		IFS=$'\t' read -r _ alias _ <<<"$row"

		case "$key" in
		ctrl-y)
			# Copy the resolved `HostName`, not the alias, because callers usually want the
			# concrete network name/IP for pasting elsewhere.
			IFS=$'\t' read -r _ hostname < <(resolve_host_identity "$alias")
			copy_hostname "$hostname" || true
			continue
			;;
		ctrl-x)
			local custom_user
			read -rp 'User: ' custom_user
			[[ -z "$custom_user" ]] && continue
			launch_custom_user_session "$alias" "$custom_user"
			;;
		'' | enter)
			launch_default_session "$alias"
			;;
		*)
			continue
			;;
		esac

		break
	done
}

main "$@"
