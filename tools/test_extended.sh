#!/usr/bin/env bash

# Extended tests: Run a few more options other than make check

set -xe #exit on fail

# Defaults
cpus=1
S=$RANDOM
MAKE=make
READLINK=readlink
test_level=check
build_opt=''
msg=''

# Override defaults if exist
command -V gmake >/dev/null 2>&1 && MAKE=gmake
command -V greadlink >/dev/null 2>&1 && READLINK=greadlink
[ -n "$CC" ] && build_opt+="CC=$CC "
[ -n "$AS" ] && build_opt+="AS=$AS "

out="$PWD"
src=$($READLINK -f $(dirname $0))/..
source $src/tools/test_tools.sh
cd "$src"

# Run on mult cpus
if command -V lscpu >/dev/null 2>&1; then
    cpus=`lscpu -p | tail -1 | cut -d, -f 2`
    cpus=$(($cpus + 1))
elif command -V sysctl; then
    if sysctl -n hw.ncpu >/dev/null 2>&1; then
	cpus=$(sysctl -n hw.ncpu)
	cpus=$(($cpus + 1))
    fi
fi
echo "Using $cpus cpu threads"

if [ -z "$S" ]; then
    S=`tr -cd 0-9 </dev/urandom | head -c 4 | sed -e 's/^0*/1/g'`
    [ "$S" -gt 0 ] 2> /dev/null || S="123"
fi
msg+="Running with TEST_SEED=$S".$'\n'

# Fix Darwin issues
if uname | grep -q 'Darwin' 2>&1; then
    export SED=`which sed`
fi

# Check for test libs to add
if command -V ldconfig >/dev/null 2>&1; then
    if ldconfig -p | grep -q libz.so; then
	test_level=test
	msg+=$'With extra tests\n'
    fi
    if ldconfig -p | grep -q libefence.so; then
	build_opt+="LDFLAGS+='-lefence' "
	msg+=$'With efence\n'
    fi
fi

# Std makefile build test
$MAKE -f Makefile.unx clean
test_start "extended_build_test"
time $MAKE -f Makefile.unx -j $cpus $build_opt arch=aarch64 host_cpu=aarch64
test_end "extended_build_test" $?
msg+=$'Std makefile build: Pass\n'

# Std makefile build perf tests
test_start "extended_perf_test"
time $MAKE -f Makefile.unx -j $cpus perfs arch=aarch64 host_cpu=aarch64
test_end "extended_perf_test" $?
msg+=$'Std makefile build perf: Pass\n'
