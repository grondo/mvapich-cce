Summary:  MVAPICH MPI library and tools
Name:     See META
Version:  See META
Release:  See META

#
#  Grab compiler we're building from directly from our name:
#
%{expand: %%global compiler %(echo %{name} | sed -r 's/^mvapich-[0-9.]+-//')}
%{expand: %%global mpi_version %(echo %{name} | sed -r 's/^mvapich-([0-9.]+).*/\1/')}
%{expand: %%global _with_%{compiler} 1}

%define pkgname        mvapich
%define module_path    /opt/modules/modulefiles/

# Build shmem-only version
%{!?buildshmem:%define buildshmem  0}

# Build -O0 debug version (including sources)
%{!?builddebug:%define builddebug  0}
%if 0%{?chaos}
%define buildshmem              1
%define version_in_install_path 1
%{?_with_gnu:%global builddebug 1}
%endif

Source:  
Group: Development/Languages
License: GPL
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
AutoReq: no
# BuildRequires: panfs-devel
BuildRequires: libibverbs-devel 
BuildRequires: libibumad-devel 
BuildRequires: libibcommon-devel
BuildRequires: popt 
BuildRequires: elfutils-libelf-devel elfutils-devel libsysfs-devel
BuildRequires: environment-modules
BuildRequires: module_helper
BuildRequires: infinipath-psm-devel
# Need SLURM for libpmi
BuildRequires: slurm-devel

%{?_with_lustre:BuildRequires: lustre}

#
#  Adjust BuildRequires for compiler packages
#
#  Note: addition of compat-libstdc++-33 seems to be required here,
#   even though compilers have a Requires for this pkg. The problem
#   appears to be that YUM will only install the i386 version of this
#   pkg (which is required), if the pkg being built lists it as a
#   BuildRequires, o/w only the 64bit package is installed, and the
#   compilers don't work under mock.
#
%{?_with_gnu:BuildRequires: gcc-gfortran >= 4.1.2 gcc-c++}
%{?_with_intel:BuildRequires: intel-11.1 }
%{?_with_pgi:BuildRequires: pgi-8.0.1 }
%{?_with_pathscale:BuildRequires: pathscale-3.2.99 }

#
#  Require base modules stuff:
#
Requires: environment-modules module_helper

#
#  Adjust Requires based on compilers
#
#%{?_with_intel:Requires: intel-10.0.025 }
#%{?_with_pgi:Requires: pgi-7.0.6}
#%{?_with_pathscale:Requires: pathscale}

%define debug_package %{nil}
%define __check_files %{nil}

%ifnarch i386 i686 i586
%define __arch_install_post %{nil}
%endif

%define mvapich_prefix /opt/mvapich

Summary: MVAPICH package for OpenIB/gen2
Group: Development/Languages
Requires: libibverbs libibumad popt elfutils elfutils-libelf libsysfs
Obsoletes: mvapich-%{compiler}
Provides: mvapich-%{compiler}

%description
MVAPICH (http://nowlab.cse.ohio-state.edu/projects/mpi-iba -- from
The Ohio State University) is an MPI-1 implementation based on MPICH
(http://www.mcs.anl.gov/mpi/mpich -- from Argonne National Laboratory)
and MVICH (MPI for VIA -- from NERSC).

MVAPICH runs on OpenIB/Gen2 (http://www.openfabrics.org -- from
OpenFabrics), which is an interface for accessing Infiniband interconnect
hardware.

MVAPICH supports many features for high performance scalability,
portability, and fault tolerance.

MPICH is an open-source and portable implementation of the Message-Passing
Interface.  MPICH includes all of the routines in MPI 1.2, along with
the I/O routines from MPI-2 and some additional routines from MPI-2,
including those supporting MPI Info and some of the additional datatype
constructors.

%package shmem
Summary: MVAPICH MPI library built for shmem only.
Group: Development/Languages
Obsoletes: mvapich-%{compiler}-shmem
Provides: mvapich-%{compiler}-shmem

%description shmem
MVAPICH (http://nowlab.cse.ohio-state.edu/projects/mpi-iba -- from
The Ohio State University) is an MPI-1 implementation based on MPICH
(http://www.mcs.anl.gov/mpi/mpich -- from Argonne National Laboratory)
and MVICH (MPI for VIA -- from NERSC).

MVAPICH runs on OpenIB/Gen2 (http://www.openfabrics.org -- from
OpenFabrics), which is an interface for accessing Infiniband interconnect
hardware.

MVAPICH supports many features for high performance scalability,
portability, and fault tolerance.  MPICH is an open-source and portable
implementation of the Message-Passing Interface.  MPICH includes all of
the routines in MPI 1.2, along with the I/O routines from MPI-2 and some
additional routines from MPI-2, including those supporting MPI Info and
some of the additional datatype constructors.

This version of MVAPICH has been compiled with shmem support


%package psm
Summary: MVAPICH MPI library built for InfiniPath
Group: Development/Languages
Provides: mvapich-%{compiler}

%description psm
MVAPICH (http://nowlab.cse.ohio-state.edu/projects/mpi-iba -- from
The Ohio State University) is an MPI-1 implementation based on MPICH
(http://www.mcs.anl.gov/mpi/mpich -- from Argonne National Laboratory)
and MVICH (MPI for VIA -- from NERSC).

MVAPICH supports many features for high performance scalability,
portability, and fault tolerance.  MPICH is an open-source and portable
implementation of the Message-Passing Interface.  MPICH includes all of
the routines in MPI 1.2, along with the I/O routines from MPI-2 and some
additional routines from MPI-2, including those supporting MPI Info and
some of the additional datatype constructors.

This version of MVAPICH has been compiled with InfiniPath support.



%if %{builddebug}
%package debug
Summary: Debug version and sources for MVAPICH library.
Group: Development/Languages
Requires: libibverbs libibumad popt elfutils elfutils-libelf libsysfs
Obsoletes: mvapich-%{compiler}-debug
Provides: mvapich-%{compiler}

%description debug
MVAPICH IB/gen2 libraries built with -O0. Also includes sources
for debugging purposes.

%package debug-shmem 
Summary: Debug version and sources for MVAPICH shmem library.
Group: Development/Languages
Obsoletes: mvapich-%{compiler}-debug-shmem
provides: mvapich-%{compiler}

%description debug-shmem
MVAPICH shared memory libraries built with -O0. Also includes sources
for debugging purposes.

%package debug-psm 
Summary: Debug version and sources for MVAPICH shmem library.
Group: Development/Languages
Obsoletes: mvapich-%{compiler}-debug-shmem
Provides: mvapich-%{compiler}

%description debug-psm
MVAPICH shared memory libraries built with -O0. Also includes sources
for debugging purposes.

%endif # builddebug

%package doc
Summary: MVAPICH MPI library documentation.
Group: Documentation

%description doc
Documentation for MVAPICH

%prep
%setup -n %{name} -q
rm -rf ${RPM_BUILD_ROOT}

%install

mkdir -p $RPM_BUILD_ROOT

#
#  Load modules for 3rd party compilers
#
PREREQ=""
%if 0%{?chaos}
 . /usr/share/[mM]odules/init/bash
 if test "%{compiler}" != "gnu"; then
   module load %{compiler}
   PREREQ="prereq %{compiler}"
 fi
%endif

# XXX this should go in Makefile but I can't seem to figure out how 
# to get it to work right -jg
export F77_GETARGDECL=" "
VARIANTS=gen2
COMPILERS=%{compiler}

VARIANTS="gen2 shmem psm"

%if %{builddebug}
for cc in $COMPILERS; do
  COMPILERS="$COMPILERS ${cc}-debug"
done
#
# Build in an extra long path in order to ensure debugedit has 
#  enough space to rewrite debug info
export LONG_PATH_HACK=1
%endif

export NESTED_INSTALL=0
export VERSION=%{mpi_version}
export INCLUDE_VARIANT_IN_PREFIX=1
export VARIANTS COMPILERS

make -f chaos/Makefile.chaos -j4 DESTDIR="$RPM_BUILD_ROOT" \
         MVAPICH_PREFIX=%{mvapich_prefix} \
		 VERSION=%{mpi_version}

# Install a copy of docs and modulesfiles for each subpackage
#

mpi_compiler_links()
{
    cc=$1
    dest=$2

    case ${cc} in
    gnu*)
      links="gcc:cc g++:CC gCC:CC g77:f77 gfortran:f90" ;;
    intel*)
      links="icc:cc icpc:CC ifort:f90" ;;
    pgi*)
      links="pgicc:cc pgiCC:CC pgif90:f90" ;;
    *)
      exit 1
    esac

    for l in $links; do
        target=mpi${l##*:}
        link=mpi${l//:*}
        ln -Ts ${dest}/${target} $RPM_BUILD_ROOT/${dest}/${link}
    done
}

for cc in $COMPILERS; do
   for variant in $VARIANTS; do
       basedir=%{mvapich_prefix}-${cc}-${variant}-%{mpi_version}
       docdir=$RPM_BUILD_ROOT/${basedir}/doc

       mpi_compiler_links ${cc} ${basedir}/bin

       mkdir -p -m755 ${docdir}   
	   install -m644 NEWS CHANGELOG ${docdir}


	   module_subdir=mvapich-${cc}-${variant}
	   module_name=%{mpi_version}
	   module_path=%{module_path}/${module_subdir}/${module_name}

	   mkdir -p -m755 $RPM_BUILD_ROOT/%{module_path}/${module_subdir}
	   modfile=$RPM_BUILD_ROOT/${module_path}


	   install -m644 chaos/module.template $modfile

	   sed --in-place \
		   -e "s/__PREREQ__/$PREREQ/" \
		   -e 's/__NAME__/%{pkgname}/' \
		   -e 's/__VERSION__/%{mpi_version}/' \
		   -e "s/__COMPILER__/$cc/" \
		   -e "s/__DEVICE__/$variant/" \
		   $modfile
   done
done


%clean
rm -rf $RPM_BUILD_ROOT

###############################################################################

%define mvapich_path() %{mvapich_prefix}-%{compiler}-%{1}-%{mpi_version}

%files 
%defattr(-,root,root)
# % doc doc/README.alternatives NEWS ChangeLog mvapich/CHANGELOG
%dir %{mvapich_path gen2}
%{mvapich_path gen2}/*
%{module_path}/%{pkgname}-%{compiler}-gen2/%{mpi_version}

%files psm
%defattr(-,root,root)
%dir %{mvapich_path psm}
%{mvapich_path psm}/*
%{module_path}/%{pkgname}-%{compiler}-psm/%{mpi_version}

%if %{?builddebug:1}%{!?builddebug:0}
%files debug
%defattr(-,root,root)
%dir %{mvapich_path debug-gen2}
%{mvapich_path debug-gen2}/*
%{module_path}/%{pkgname}-%{compiler}-debug-gen2/%{mpi_version}

%files debug-psm
%defattr(-,root,root)
%dir %{mvapich_path debug-psm}
%{mvapich_path debug-psm}/*
%{module_path}/%{pkgname}-%{compiler}-debug-psm/%{mpi_version}
%endif # builddebug

%if %{?buildshmem:1}%{!?buildshmem:0}
%files shmem
%defattr(-,root,root)
%dir %{mvapich_path shmem}
%{mvapich_path shmem}/*
%{module_path}/%{pkgname}-%{compiler}-shmem/%{mpi_version}

%if %{?builddebug:1}%{!?builddebug:0}
%files debug-shmem
%defattr(-,root,root)
%dir %{mvapich_path debug-shmem}
%{mvapich_path debug-shmem}/*
%{module_path}/%{pkgname}-%{compiler}-debug-shmem/%{mpi_version}

%endif # builddebug
%endif # buildshmem

%changelog
