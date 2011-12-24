#!/usr/bin/perl
 
# This simple perl script parses the MVICH parameter file
# specified on the mpirun command line.  It is used only
# used when MVICH is compiled to work with the MPD job
# manager.  
# It parses the parameter file and converts the VIADEV
# parameters into a string suitable for use as shell
# environment vriables.  The individual MPI processes
# will see these parameters as environment variables.

#######################################################################
# Copyright (C) 1999-2001 The Regents of the University of California
# (through E.O. Lawrence Berkeley National Laboratory), subject to
# approval by the U.S. Department of Energy.
#
# Your use of this software is under license -- the license agreement is
# attached and included in the MVICH top-level directory as LICENSE.TXT
# or you may contact Berkeley Lab's Technology Transfer Department at
# TTD@lbl.gov.
#
# NOTICE OF U.S. GOVERNMENT RIGHTS. The Software was developed under
# funding from the U.S. Government which consequently retains certain
# rights as follows: the U.S. Government has been granted for itself and
# others acting on its behalf a paid-up, nonexclusive, irrevocable,
# worldwide license in the Software to reproduce, prepare derivative
# works, and perform publicly and display publicly. Beginning five (5)
# years after the date permission to assert copyright is obtained from
# the U.S. Department of Energy, and subject to any subsequent five (5)
# year renewals, the U.S. Government is granted for itself and others
# acting on its behalf a paid-up, nonexclusive, irrevocable, worldwide
# license in the Software to reproduce, prepare derivative works,
# distribute copies to the public, perform publicly and display
# publicly, and to permit others to do so.
#######################################################################

use Time::Local;
use Getopt::Std;
use File::Find;
 
$USAGE = "$0 [-d] param_file_name";

getopts('d');
$debug = $opt_d;

$numargs = scalar(@ARGV);
if ($debug) {
    print "Num args = $numargs\n";
    my $i = 0;
    foreach $arg (@ARGV) {
	printf STDERR ("Arg %3s = %s\n",$i,$arg);
    }
}

if ($numargs != 1) {
    print STDERR $USAGE;
}

$param_file = $ARGV[0];
open(F,"< $param_file") || die "Cant open $param_file";

$env = "";
while(<F>) {
    s/\s+//g;
    next if /^$/;
    next if /^\#/;
    next if !/^VIADEV_/;
    print "$_\n" if $debug;
    $env .= "$_ ";
}
close(F);

print "$env\n";

exit(0);

    
