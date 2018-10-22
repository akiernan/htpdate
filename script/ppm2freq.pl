#! /usr/bin/perl
#
# Convert the PPM values found in syslog to adjtimex parameters

$PPM = $ARGV[0];
printf ("%6.2f sec/day\n", $PPM * 0.0864) ;
printf ("%6.0f TICKS\n", $PPM / 100 ) ;
print abs($PPM) % 100 * 65536 *abs($PPM)/$PPM, " FREQUENCY\n" ;

