#!/bin/bash

version=0.1.1
protocol=
distrib=unknown
distribver=0.0
tmpdir=$TMPDIR
if test -z "$tmpdir"; then
	tmpdir="/tmp"
fi
tmpdir=$tmpdir/alsa-compile-script
baseurl="http://www.alsa-project.org/snapshot/?package="
package=alsa-driver
packagedefault=true
url=
urldefault=
gittree="git://git.alsa-project.org/"
usegit=false
httpdownloader=
compile=
install=
quiet=
yes=
kernelmodules=
kmodlist=
kmodremove=
kmodclean=
depmodbin=
modinfobin=
runargs=
patches=
kmodmesg=
withdebug=

usage() {
	echo "Usage: $0 [OPTION]..."
	cat <<EOF

This is a script version $version doing ALSA code compilation and installation.
Report any problems to <alsa-devel@alsa-project.org> mailing list.

Operation modes:
  -h, --help		print this help, then exit
  -e, --examples	print text with examples, then exit
  -q, --quiet		quiet mode
  -y, --yes		do not ask any questions - answer is always yes
  -c, --clean[=package]	do overall clean, do package clean
  --url=url		package url
  --git=giturl		work with git tree
  --compile		force compilation
  --install		install binaries and headers
  --patch=patch		apply code patch (can be used multiple times)
  --tmpdir=dir		set temporary directory (overrides TMPDIR envval)
  --kmodules[=mods]	reinsert kernel modules or insert specified modules
  			to current kernel
  --kmodlist		list ALSA toplevel kernel modules
  --kmodremove		remove ALSA kernel modules from kernel
  --kmodclean		remove installed ALSA kernel modules from /lib/modules
  --kmodmesg		show ALSA kernel related messages
  --run program [args]  run a program using fresh alsa-lib
  --with-debug=dbgopt	set debug options to dbgopt (for alsa-driver)

Package selection:
  --driver		compile alsa-driver package (default)
  --lib			compile alsa-lib package
  --utils		compile alsa-utils package
  --plugins		compile alsa-plugins package
  --firmware		compile alsa-firmware package
  --oss			compile alsa-oss package
EOF
}

examples() {
	cat <<EOF
$0 examples:

  Install latest ALSA driver snapshot to system:

	alsa-compile.sh --driver --install

  Try new ALSA driver snapshot without installing it to system. The existing
  ALSA kernel modules are replaced with fresh ones:

	alsa-compile.sh --driver --kmodules

  Do both (installation and modules replacement):
  
  	alsa-compile.sh --driver --install --kmodules

  Install latest ALSA library snapshot to system:
  
  	alsa-compile.sh --lib --install

  Run application using latest ALSA library snapshot. The new alsa-lib is not
  installed to system:
  
  	alsa-compile.sh --run <your_program_including_arguments>

  Do not want to fetch sources? Use file: protocol to specify current or
  explicit directory containing sources:
  
  	alsa-compile.sh --driver --url=file:///home/alsa/alsa-driver
EOF
}

while :
do
	case "$1" in
	-h|--help)
		usage
		exit 0
		;;
	-e|--examples)
		examples
		exit 0
		;;
	-q|--quiet)
		quiet=true ;;
	-y|--yes)
		yes=true ;;
	-c*|--clean*)
		rmpkg=
		case "$#,$1" in
		*,*=*)
			rmpkg=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			;;
		*)
			rmpkg="$2"
			shift ;;
		esac
		if test -z $rmpkg; then
			echo -n "Removing tree $tmpdir:"
			if test -d $tmpdir; then
				if ! rm -rf $tmpdir; then
					echo " failed"
					exit 1
				fi
			fi
			echo " success"
		else
			echo -n "Removing package $package:"
			rm $tmpdir/environment.* 2> /dev/null
			packagedir="$tmpdir/$package.dir"
			if test "$package" = "alsa-driver"; then
				rm -rf $tmpdir/modules.*
				rm -rf $tmpdir/run.awk
			fi
			if test -r $packagedir; then
				tree=$(cat $packagedir)
				if test -d $tmpdir/$tree; then
					if ! rm -rf $tmpdir/$tree; then
						echo " failed"
						exit 1
					fi
				fi
				rm -f $packagedir
				echo " success"
			elif test -d $package; then
				rm -rf $package
				if test "$package" = "alsa-driver"; then
					rm -rf alsa-kmirror 2> /dev/null
				fi
				echo " success"
			else
				echo " success"
			fi
		fi
		exit 0
		;;
	--url*)
		case "$#,$1" in
		*,*=*)
			url=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			url="$2"
			shift ;;
		esac
		;;
	--git*)
		case "$#,$1" in
		*,*=*)
			url=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			url="$gittree" ;;
		*)
			url="$2"
			shift ;;
		esac
		;;
	--with-debug*)
		case "$#,$1" in
		*,*=*)
			withdebug=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			withdebug="$2"
			shift ;;
		esac
		;;
	--compile)
		compile=true
		;;
	--install)
		install=true
		;;
	--patch)
		case "$#,$1" in
		*,*=*)
			patch=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			patch="$2"
			shift ;;
		esac
		patches="$patches $patch"
		;;
	--driver|--lib|--utils|--plugins|--firmware|--oss)
		pkg="$1"
		pkg=${pkg:2}
		package="alsa-$pkg"
		packagedefault=
		test -z "$url" && url="$baseurl$package"
		;;
	--run)
		package="alsa-lib"
		packagedefault=
		test -z "$url" && url="$baseurl$package"
		shift
		runargs="$@"
		break
		;;
	--tmpdir*)
		case "$#,$1" in
		*,*=*)
			tmpdir=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			tmpdir="$2"
			shift ;;
		esac
		tmpdir="$tmpdir/alsa-compile-script"
		;;
	--kmodules*)
		case "$#,$1" in
		*,*=*)
			kernelmodules=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			kernelmodules="reinstall" ;;
		*)
			kernelmodules="$2"
			shift ;;
		esac
		;;
	--kmodlist)
		kmodlist=true
		;;
	--kmodremove)
		kmodremove=true
		;;
	--kmodclean)
		dir="/lib/modules/`uname -r`/alsa/updates"
		echo "Removing kernel modules in $dir:"
		if test -d $dir; then
			if ! rm -rf $dir; then
				echo >&2 "Cannot remove tree $dir"
				exit 1
			fi
		fi
		echo "  success"
		exit 0
		;;
	--kmodmesg)
		kmodmesg=true
		;;
	*)
		test -n "$1" && echo "Unknown parameter '$1'"
		break
		;;
	esac
	shift
done
if test -z "$url"; then
	url="$baseurl$package"
	urldefault=true
fi

question_bool() {
	if test "$yes" = "yes"; then
		echo "true"
	else
		echo >&2 -n "$1 (Y/ ) "
		read i
		i=${i:0:1}
		if test "$i" = "Y" -o "$i" = "y"; then
			echo "true"
		else
			echo "false"
		fi
	fi
}	

check_distribution() {
	distrib=$(lsb_release -ds 2> /dev/null | cut -d ' ' -f 1)
	distrib=${distrib:1}
	distribver=$(lsb_release -rs 2> /dev/null)
	if test -z "$distrib"; then
		echo >&2 "Unable to determine Linux distribution!"
		exit 1
	fi
	if test -z "$distribver"; then
		echo >&2 "Unable to determine Linux distribution version!"
		exit 1
	fi
	echo "Detected Linux distribution '$distrib $distribver'."
}

is_rpm_installed() {
	a=$(rpm -qi $1 | head -1 | cut -d ' ' -f 1)
	if test "$a" = "Name"; then
		echo "frue"
	else
		echo "false"
	fi
}

install_package() {
	echo "Trying to install package '$1':"
	case "$distrib" in
	openSUSE)
		if zypper install $1; then
			echo "Cannot install package '$1'."
			exit 1
		fi
		;;
	Fedora)
		if yum install $1; then
			echo "Cannot install package '$1'."
			exit 1
		fi
		;;
	*)
		echo >&2 "install_package: Unsupported distribution $distrib"
		exit 1
	esac
	if test $(is_rpm_installed $1) = "false" ; then
		echo "Package '$1' was not installed."
		exit 1
	fi
	echo "  installed"
}

check_kernel_source() {
	echo "Checking kernel build environment:"
	case "$distrib" in
	openSUSE)
		if test $(is_rpm_installed kernel-source) = "false" ; then
			install_package kernel-source
		fi
		;;
	Fedora)
		if test $(uname --kernel-release | grep -q '\.PAE$'); then
			kernel_devel=kernel-PAE-devel
		else
			kernel_devel=kernel-devel
		fi
		if test $(is_rpm_installed $kernel_devel) = "false" ; then
			install_package $kernel_devel
		fi
		;;
	*)
		echo >&2 "check_kernel_source: Unsupported distribution $distrib"
		exit 1
		;;
	esac
	echo "  passed"
}

check_environment() {
	env="$tmpdir/environment.base"
	if ! test -s $env; then
		if ! test -d $tmpdir ; then
			mkdir -p $tmpdir
			if ! test -d $tmpdir; then
				echo >&2 "Unable to create directory $tmpdir"
				exit 1
			fi
		fi
		echo "Using temporary tree $tmpdir"
		check_distribution
		test -x /bin/depmod && depmodbin=/bin/depmod
		test -x /sbin/depmod && depmodbin=/sbin/depmod
		if test -z "$depmodbin"; then
			echo >&2 "Unable to find depmod utility"
			exit 1
		fi	
		test -x /bin/modinfo && modinfobin=/bin/modinfo
		test -x /sbin/modinfo && modinfobin=/sbin/modinfo
		if test -z "$modinfobin"; then
			echo >&2 "Unable to find modinfo utility"
			exit 1
		fi	
		echo "protocol=$protocol" >> $env
		echo "distrib=\"$distrib\"" >> $env
		echo "distribver=\"$distribver\"" >> $env
		echo "url=\"$url\"" >> $env
		echo "package=\"$package\"" >> $env
		echo "depmodbin=\"$depmodbin\"" >> $env
		echo "modinfobin=\"$modinfobin\"" >> $env
		b=$(basename $env)
		echo "File $b has been created."
	else
		b=$(basename $env)
		echo "File $b is cached."
		opackage="$package"
		ourl="$url"
		. $env
		if test "$packagedefault" != "true"; then
			package="$opackage"
		fi
		if test "$urldefault" != "true"; then
			url="$ourl"
		fi
	fi
}

check_compilation_environment() {
	env="environment.compile"
	if ! test -s $env; then
		a=$(wget --version | head -1 | cut -d ' ' -f 2)
		if test "$a" = "Wget"; then
			httpdownloader="wget"
		else
			a=$(curl --version | head -1 | cut -d ' ' -f 1 )
			if test "$a" = "curl"; then
				httpdownloader="curl"
			fi
		fi
		if test -z "$httpdownloader"; then
			echo >&2 "Unable to determine HTTP downloader."
			exit 1
		fi
		a=$(git --version | head -1 | cut -d ' ' -f 1)
		if test "$a" != "git"; then
			install_package git
		else
			echo "Program git found."
		fi
		a=$(autoconf --version | head -1 | cut -d ' ' -f 1)
		if test "$a" != "autoconf"; then
			install_package autoconf
		else
			echo "Program autoconf found."
		fi
		a=$(gcc --version | head -1 | cut -d ' ' -f 1)
		if test "$a" != "gcc" ; then
			install_package gcc
		else
			echo "Program gcc found."
		fi
		a=$(patch --version | head -1 | cut -d ' ' -f 1)
		if test "$a" != "patch" ; then
			install_package patch
		else
			echo "Program patch found."
		fi
		a=$(diff --version | head -1 | cut -d ' ' -f 1)
		if test "$a" != "diff" ; then
			install_package diffutils
		else
			echo "Program diff found."
		fi
		if test "$protocol" = "git"; then
			a=$(git --version | head -1 | cut -d ' ' -f 1)
			if test "$a" != "git"; then
				install_package git
			else
				echo "Program git found."
			fi
		fi
		check_kernel_source
		echo "httpdownloader=$httpdownloader" >> $env
		echo "File $env has been created."
	else
		echo "File $env is cached."
		. $env
	fi
}

download_http_file() {
	echo "Downloading '$1':"
	case "$httpdownloader" in
	wget)
		wget -q -O $2 $1
		;;
	curl)
		curl -s -o $2 $1
		;;
	*)
		echo >&2 "Unknown httpdownloader '$httpdownloader'."
		exit 1
		;;
	esac
	size=$(stat -c%s $2)
	if test -z "$size" -o $size -le 0; then
		echo "  failed ($size)"
		echo >&2 "Unable to download file '$1'."
		exit 1
	else
		echo "  success ($size bytes)"
	fi
}

do_compile() {
	cmd="./gitcompile"
	case "$package"  in
	alsa-driver)
		dbgopt="$withdebug"
		test -z "$dbgopt" && dbgopt="full"
		cmd="./gitcompile --with-debug=$dbgopt --with-isapnp=yes --with-sequencer=yes --with-moddir=updates/alsa"
		;;
	esac
	if test -z "$patches"; then
		case "$package" in
		alsa-driver)
			test -r acore/hwdep.o && cmd="make"
			;;
		alsa-lib)
			test -r src/.libs/libasound.so.2.0.0 && cmd="make"
			;;
		esac
	else
		for patch in $patches; do
			pstrip=1
			if ! test patch -s -p$pstrip -N --dry-run < $patch; then
				pstrip=0
				if ! test patch -s -p$pstrip -N --dry-run < $patch; then
					echo >&2 "Cannot apply patch $patch"
					exit 1
				fi
			fi
			echo "Applying patch $patch: "
			if ! test patch -p$pstrip -N < $patch; then
				exit 1
			fi
		done
	fi
	echo "Running $cmd:"
	if ! $cmd; then
		a=$(pwd)
		echo >&2 "Working directory $a"
		echo >&2 "Compilation of $package tree failed."
		echo >&2 "Report this problem to the <alsa-devel@alsa-project.org> mailing list."
		exit 1
	fi
}

do_install() {
	if ! test "$UID" -eq 0 ; then
		echo >&2 "Root priviledges required for installation."
		echo >&2 "Login as root to continue."
		exit 1
	fi
	cmd="make install"
	case "$package" in
	alsa-driver)
		cat <<EOF
WARNING! You chose to install new ALSA kernel modules. To reuse the ALSA
kernel modules provided by the kernel package, invoke command:
  $0 --kmodclean
EOF
		if test $(question_bool "Really continue (Ctrl-C to abort)?") = "true"; then
			cmd="make install-modules"
		fi
		;;
	esac
	$cmd || exit 1
}

do_git() {
	echo "do git"
}

do_cmd() {
	echo "> $@"
	$@ || exit 1
}

kill_audio_apps() {
	pids0=$(fuser /dev/snd/* 2> /dev/null)
	pids1=$(fuser /dev/mixer* 2> /dev/null)
	pids2=$(fuser /dev/sequencer* 2> /dev/null)
	pids=
	for pid in $pids0 $pids1 $pids2; do
		pids="$pids $pid"
	done
	if ! test -z "$pids"; then
		echo
		echo "WARNING! An audio application uses ALSA driver:"
		echo
		for pid in $pids; do
			ps --no-headers -p $pids || exit 1
		done
		echo
		if test $(question_bool "Would you like to kill these apps?") = "true"; then
			for pid in $pids; do
				do_cmd kill $pid
			done
			sleep 2
			killed=
			for pid in $pids; do
				a=$(ps --no-headers -p $pids)
				if test -n "$a"; then
					do_cmd kill -9 $pid
					killed="true"
				fi
			done
			if test "$killed" = "true"; then
				sleep 2
				for pid in $pids; do
					a=$(ps --no-headers -p $pids)
					if test -n "$a"; then
						echo >&2 "Unable to kill application:"
						echo >&2 "  $a"
						exit 1
					fi
				done
			fi
		else
			echo >&2 "Terminated by user."
			exit 1
		fi
	fi
}

parse_modules() {	
	if ! test -s ../modules.dep; then
		rel=$(uname -r)
		cd modules
		for i in snd-dummy.*; do
			i1=$(echo $i | sed -e 's/dummy/dummy1/g')
			ln -sf $i $i1 || exit 1
		done
		cd ..
		pdst="xxxx/lib/modules/$rel"
		mkdir -p $pdst/modules || exit 1
		for i in modules/*.*o; do
			ln -sf ../../../../../$i $pdst/$i || exit 1
		done
		p=$(pwd)
		if ! $depmodbin -b $p/xxxx ; then
			echo >&2 "depmod failed."
			exit 1
		fi
		
		if ! cp $pdst/modules.dep ../modules.dep ; then
			echo >&2 "cp problem."
			exit 1
		fi
		rm -rf xxxx || exit 1
		echo "File modules.dep has been created."
	else
		echo "File modules.dep is cached."
	fi

	if ! test -s ../modules.top ; then
		for i in modules/*.*o; do
			pwd
			echo "$i"
			if test -r $i; then
				a=$($modinfobin $i | grep "parm:" | grep "enable:")
				if ! test -z "$a"; then
					a=$(basename $i | cut -d . -f 1)
					echo $a >> ../modules.top
				fi
			else
				echo >&2 "permissions $tmpdir/$i problem"
				exit 1
			fi
		done
		echo "File modules.top has been created."
	else
		echo "File modules.top is cached."
	fi
}

current_modules() {
	lsmod | cut -d ' ' -f 1 | grep -E "^(snd[_-]|snd$)"
}

my_rmmod() {
	while test -n "$1"; do
		if ! rmmod $1 2> /dev/null > /dev/null; then
			phase2="$phase2 $1"
		else
			echo "> rmmod $1"
		fi
		shift
	done
	for mod in $phase2; do
		echo "> rmmod $mod"
		if ! rmmod $mod ; then
			echo >&2 "Unable to remove kernel module $mod"
			exit 1
		fi
	done
}

my_insmod() {
	while test -n "$1"; do
		xmod=
		args=
		nofail=
		for x in $1; do
			if test -z "$xmod"; then
				xmod=$x
			else
				args="$args $x"
			fi
		done
		if test "$xmod" = "snd-dummy1"; then
			args="index=999"
			nofail=true
		fi
		if test -r modules/$xmod.ko; then
			mod=modules/$xmod.ko
			echo "> insmod $mod.ko $args"
			if test -n "$nofail"; then
				insmod $mod $args 2> /dev/null
			elif ! insmod $mod $args; then
				echo >&2 "Unable to insert kernel module $xmod.ko"
				exit 1
			fi
		else
			if test -r modules/$xmod.o; then
				mod=modules/$xmod.o
				echo "> insmod $mod.o $args"
				if test -n "$nofail"; then
					insmod $mod.o $args
				elif ! insmod $mod.o $args; then
					echo >&2 "Unable to insert kernel module $xmod.o"
					exit 1
				fi
			else
				echo >&2 "Unable to find kernel module $xmod"
				exit 1
			fi
		fi
		shift
	done
}

show_kernel_messages() {
	cat > $tmpdir/run.awk <<EOF
/Dummy soundcard not found or device busy/ { delete lines }
	{ lines[length(lines)+1] = \$0 }
END	{
		for (x = 3; x <= length(lines); x++)
			print prefix lines[x]
	}
EOF
	dmesg | awk -f $tmpdir/run.awk -v prefix="$1"
	rm $tmpdir/run.awk || exit 1
}

do_kernel_modules() {
	usermods="$@"
	curmods=$(current_modules)
	if test -z "$curmods" -a -z "$usermods"; then
		echo >&2 "Unable to determine current ALSA kernel modules."
		exit 1
	fi
	if test -n "$usermods"; then
		loadmods="$usermods"
		dst="../modules.user"
		rm -f $dst || exit 1
	else
		loadmods="$curmods"
		dst="../modules.insmod"
	fi
	parse_modules
	if ! test -s $dst; then
		topmods=$(cat ../modules.top)
		cat > run.awk <<EOF
function basename(name) {
	split(name, y, "[/.]")
	return y[length(y)-1]
}
function skipempty(dst, src, delim,   x) {
	split(src, y, delim)
	for (x in y) {
		if (length(y[x]) > 0)
			dst[x] = y[x]
	}
}
function skipemptyuser(dst, src, delim,   x, m) {
	split(src, y, delim)
	for (x in y) {
		if (length(y[x]) > 0) {
			m = y[x]
			if (substr(m, 0, 4) != "snd-")
				m = "snd-" m
			dst[x] = m
		}
	}
}
function moduleindex(mod,   x) {
	for (x in modules) {
		if (modules[x] == mod)
			return x
	}
	return 0;
}
function addmodule(mod,    v, j, k) {
	if (moduleindex(mod) > 0)
		return
	v = split(deps[mod], j, " ")
	for (k = 1; k <= v; k++)
		addmodule(j[k])
	modules[length(modules)+1] = mod
}
function addslavemodules(mod, exclude,   m, v, j, k)
{
	for (m in deps) {
		v = split(deps[m], j, " ")
		for (k = 1; k <= v; k++) {
			if (j[k] == mod && m != exclude) {
				addmodule(m)
			}
		}
	}
}
function addtopmodule(mod,   v, j, k, z)
{
	v = split(deps[mod], j, " ")
	for (k = 1; k <= v; k++) {
		for (z = 1; z <= length(alldeps); z++) {
			if (j[k] == alldeps[z])
				addslavemodules(j[k], mod)
		}
	}
	addmodule(mod)
	addslavemodules(mod)
}
BEGIN   {
		skipempty(akmods, kmods, ",")
		if (length(usermods) > 0)
			skipemptyuser(acurmods, usermods, " ")
		else
			skipempty(acurmods, curmods, " ")
		skipempty(atopmods, topmods, " ")
		for (b in atopmods)
			alldeps[b] = atopmods[b]
		# hack to load HDA codec modules
		alldeps[length(alldeps)+1] = "snd-hda-codec"
	}
	{
		split(\$0, a, "[: ]")
		first = 1
		for (b = 1; b <= length(a); b++) {
			if (length(a[b]) <= 0)
				continue
			base = basename(a[b])
			if (first) {
				mod = base
				deps[mod] = ""
				first = 0
			} else {
				if (length(deps[mod]) > 0)
					deps[mod] = deps[mod] " " base
				else
					deps[mod] = base
			}
		}
		delete a

	}
END     {
		addmodule("snd-page-alloc")
		addmodule("snd-dummy1")

		# all direct toplevel modules dependencies
		for (mod in deps) {
			split(deps[mod], mods, " ")
			for (d in atopmods) {
				if (mod != atopmods[d])
					continue
				for (b in acurmods) {
					mod1 = acurmods[b]
					v = gsub("_", "-", mod1)
					if (mod != acurmods[b] && mod != mod1)
						continue
					addtopmodule(mod)
					break
				}
			}
		}

		addmodule("snd-seq")
		addmodule("snd-mixer-oss")
		for (b in modules) {
			if ("snd-pcm" == modules[b])
				addmodule("snd-pcm-oss")
		}

		for (b = 1; b <= length(modules); b++) {
			mod = modules[b]
			if (length(mod) > 0)
				print mod
		}
	}
EOF
		awk -f run.awk -v kmods="$kernelmodules" \
		       -v curmods="$curmods" \
		       -v usermods="$usermods" \
		       -v topmods="$topmods" \
				../modules.dep > $dst || exit 1
		rm run.awk || exit 1
		if ! test -s $dst; then
			if test -n "$usermods"; then
				echo >&2 "Unable to determine specified ALSA kernel modules."
			else
				echo >&2 "Unable to determine current ALSA kernel modules."
			fi
			exit 1
		fi
		if test -z "$usermods"; then
			echo "File modules.insmod has been created."
		fi
	else
		if test -z "$usermods"; then
			echo "File modules.insmod is cached."
		fi
	fi
	kill_audio_apps
	my_rmmod $curmods
	dmesg > ../dmesg.txt.1
	my_insmod $(cat $dst)
	echo "Kernel modules ready:"
	cat /proc/asound/cards
	if ! alsactl init 2> /dev/null > /dev/null ; then
		echo "Mixer initialized using 'alsactl init' command."
	else
		echo "Use a mixer application (like alsamixer) to set reasonable volume levels."
	fi
	dmesg > ../dmesg.txt.2
	show_kernel_messages " [kmsg] "
}

kernel_modules() {
	if test "$package" != "alsa-driver"; then
		echo >&2 "--kernel-modules works only for alsa-driver package."
		exit 1
	fi
	if test "$kernelmodules" = "reinstall"; then
		do_kernel_modules
	else
		a=$(echo $kernelmodules | sed -e 's/,/ /g')
		do_kernel_modules $a
	fi
}

kernel_modules_list() {
	if test "$package" != "alsa-driver"; then
		echo >&2 "--kmodlist works only for alsa-driver package."
		exit 1
	fi
	parse_modules
	topmods=$(cat ../modules.top)
	for mod in $topmods; do
		desc=$($modinfobin -F description modules/$mod.*o)
		echo "$mod: $desc"
	done
}

kernel_modules_remove() {
	if test "$package" != "alsa-driver"; then
		echo >&2 "--kmodremove works only for alsa-driver package."
		exit 1
	fi
	curmods=$(current_modules)
	if test -z "$curmods"; then
		echo "No ALSA kernel modules to remove."
		exit 0
	fi
	kill_audio_apps
	my_rmmod $curmods
	echo "ALSA kernel modules removed."
}

git_clone() {
	do_cmd git clone "$1$2.git" "$2"
}

rundir=$(pwd)
export LANG=C
if test "$kmodmesg" = "true"; then
	show_kernel_messages
	exit 0
fi
protocol=$(echo $url | cut -d ':' -f 1)
check_environment $protocol
do_cmd cd $tmpdir
if test "$kmodremove" = "true"; then
	kernel_modules_remove
	exit 0
fi
if test "$kmodlist" = "true" -a -z "$compile"; then
	packagedir="$package.dir"
	if test -r $packagedir; then
		tree=$(cat $packagedir)
	fi
	do_cmd cd $tree
	kernel_modules_list
	exit 0
fi
if test -n "$kernelmodules" -a -z "$compile"; then
	packagedir="$package.dir"
	if test -r $packagedir; then
		tree=$(cat $packagedir)
	fi
	do_cmd cd $tree
	kernel_modules
	exit 0
fi
case "$protocol" in
http|https|file)
	packagedir="$package.dir"
	check_compilation_environment $protocol
	if test -r $packagedir; then
		tree=$(cat $packagedir)
		echo "$package tree $tree is present."
		echo "Reusing it."
		echo "Use '$0 --clean=$package' command to refetch and rebuild."
	else
		snapshot="snapshot.tar.bz2"
		if test "$protocol" = "file"; then
			tree=$(echo $url | cut -d ':' -f 2)
			if test "${tree:0:1}" = "/"; then
				tree=${tree:1}
			fi
			if test "${tree:0:1}" = "/"; then
				tree=${tree:1}
			fi
			if test -r "$rundir/$tree/gitcompile"; then
				tree="$rundir/$tree"
			elif test -r "$rundir/$tree/../gitcompile"; then
				tree="$rundir/$tree/.."
			elif test -r "$rundir/$tree/../$package/gitcompile"; then
				tree="$rundir/$tree/../$package"
			fi
			if test -d "$tree"; then
				tree=$(cd "$tree" && pwd)
			else
				echo >&2 "Fatal: $package tree '$tree' not found"
				exit 1
			fi
		else
			download_http_file $url $snapshot
			do_cmd tar xjf $snapshot
			rm $snapshot
			tree=$(ls | grep $package)
		fi
	fi
	if ! test -x $tree/gitcompile ; then
		echo >&2 "Fatal: $package tree '$tree' not found"
		exit 1
	fi
	echo "Sources unpacked to $tree"
	echo $tree > $packagedir
	do_cmd cd $tree
	do_compile
	if test "$install" = "true"; then
		do_install
	else
		echo "ALSA package $package was successfully built."
		echo "Binaries are in $tmpdir/$tree directory."
	fi
	;;
git)
	packagedir="$package.dir"
	check_compilation_environment $protocol
	if test -r $packagedir ; then
		echo "$package tree $package is present."
		echo "Reusing it."
		echo "Use '$0 --clean=$package' command to refetch and rebuild."
	else
		if test "$package" = "alsa-driver"; then
			git_clone $url "alsa-kmirror"
		fi
		git_clone $url $package
		echo "alsa-driver" > $packagedir
	fi
	do_cmd cd alsa-driver
	do_compile
	if test "$install" = "true"; then
		do_install
	else
		echo "ALSA package $package was successfully built."
		echo "Binaries are in $tmpdir/$package directory."
	fi
	;;
*)
	echo >&2 "Unknown protocol '$protocol'."
	exit 1
esac

if test "$kmodlist" = "true"; then
	packagedir="$package.dir"
	if test -r $packagedir; then
		tree=$(cat $packagedir)
	fi
	do_cmd cd $tree
	kernel_modules_list
fi
if test -n "$kernelmodules"; then
	do_cmd cd $tmpdir
	packagedir="$package.dir"
	if test -r $packagedir; then
		tree=$(cat $packagedir)
	fi
	do_cmd cd $tree
	kernel_modules
	exit 0
fi
if test -n "$runargs"; then
	packagedir="alsa-lib.dir"
	if test -r $packagedir; then
		tree=$(cat $packagedir)
	fi
	f="$tmpdir/$tree/src/.libs/libasound.so.2.0.0"
	if test -r $f; then
		do_cmd "export LD_PRELOAD=$f"
		do_cmd "export ALSA_CONFIG_PATH=\"$tmpdir/$tree/src/conf/alsa.conf\""
		do_cmd $runargs
	else
		echo >&2 "Unable to find alsa-lib.so"
		exit 1
	fi
fi

exit 0