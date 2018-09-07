# Common definitions for all test suites.
# Copyright 2016 Serval Project Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

testdefs_sh=$(abspath "${BASH_SOURCE[0]}")
lbard_source_root="${testdefs_sh%/*}"
lbard_build_root="${testdefs_sh%/*}"

export TFW_LOGDIR="${TFW_LOGDIR:-$lbard_build_root/testlog}"
export PATH="$lbard_build_root:$PATH"

source "$lbard_source_root/serval-dna/testdefs.sh"
source "$lbard_source_root/serval-dna/testdefs_rhizome.sh"

EGREP=egrep

_tfw_assert_stdxxx_egrep() {
   local qual="$1"
   shift
   _tfw_getopts assertcontentgrep --$qual --stderr "$@"
   shift $((_tfw_getopts_shift - 2))
   if [ $# -ne 1 ]; then
      _tfw_error "incorrect arguments"
      return $?
   fi
   [ -r "$_tfw_process_tmp/$qual" ] || fail "no $qual" || return $?
   _tfw_get_content "$_tfw_process_tmp/$qual" || return $?
   _tfw_assert_egrep "${_tfw_opt_line_msg:+$_tfw_opt_line_msg of }$qual of ($TFWEXECUTED)" "$_tfw_process_tmp/content" "$@"
}

_tfw_assert_egrep() {
   local label="$1"
   local file="$2"
   local pattern="$3"
   local message=
   if ! [ -e "$file" ]; then
      _tfw_error "$file does not exist"
      ret=$?
   elif ! [ -f "$file" ]; then
      _tfw_error "$file is not a regular file"
      ret=$?
   elif ! [ -r "$file" ]; then
      _tfw_error "$file is not readable"
      ret=$?
   else
      local matches=$(( $($EGREP "${_tfw_opt_grepopts[@]}" --regexp="$pattern" "$file" | wc -l) + 0 ))
      local done=false
      local ret=0
      local info="$matches match"$([ $matches -ne 1 ] && echo "es")
      local oo
      tfw_shopt oo -s extglob
      case "$_tfw_opt_matches" in
      '')
         done=true
         message="${_tfw_message:-$label contains a line matching \"$pattern\"}"
         if [ $matches -ne 0 ]; then
            $_tfw_assert_noise && tfw_log "# assert $message"
         else
            _tfw_failmsg "assertion failed ($info): $message"
            ret=1
         fi
         ;;
      esac
      case "$_tfw_opt_matches" in
      +([0-9]))
         done=true
         local s=$([ $_tfw_opt_matches -ne 1 ] && echo s)
         message="${_tfw_message:-$label contains exactly $_tfw_opt_matches line$s matching \"$pattern\"}"
         if [ $matches -eq $_tfw_opt_matches ]; then
            $_tfw_assert_noise && tfw_log "# assert $message"
         else
            _tfw_failmsg "assertion failed ($info): $message"
            ret=1
         fi
         ;;
      esac
      case "$_tfw_opt_matches" in
      +([0-9])-*([0-9]))
         done=true
         local bound=${_tfw_opt_matches%-*}
         local s=$([ $bound -ne 1 ] && echo s)
         message="${_tfw_message:-$label contains at least $bound line$s matching \"$pattern\"}"
         if [ $matches -ge $bound ]; then
            $_tfw_assert_noise && tfw_log "# assert $message"
         else
            _tfw_failmsg "assertion failed ($info): $message"
            ret=1
         fi
         ;;
      esac
      case "$_tfw_opt_matches" in
      *([0-9])-+([0-9]))
         done=true
         local bound=${_tfw_opt_matches#*-}
         local s=$([ $bound -ne 1 ] && echo s)
         message="${_tfw_message:-$label contains at most $bound line$s matching \"$pattern\"}"
         if [ $matches -le $bound ]; then
            $_tfw_assert_noise && tfw_log "# assert $message"
         else
            _tfw_failmsg "assertion failed ($info): $message"
            ret=1
         fi
         ;;
      esac
      if ! $done; then
         _tfw_error "unsupported value for --matches=$_tfw_opt_matches"
         ret=$?
      fi
      tfw_shopt_restore oo
   fi
   if [ $ret -ne 0 ]; then
      _tfw_backtrace
   fi
   return $ret
}



assertStdoutEgrep() {
   _tfw_assert_stdxxx_egrep stdout "$@" || _tfw_failexit
}

assertStderrEgrep() {
   _tfw_assert_stdxxx_egrep stderr "$@" || _tfw_failexit
}
