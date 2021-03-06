# Test file created outside of h2xs framework.
# Run this like so: `perl 20-phase_simulated_gls.t'
#   wkretzsch@gmail.com     2015/04/21 15:24:17

#########################

use Test::More;
BEGIN { plan tests => 4 }

use warnings;
use strict;
$| = 1;
use Data::Dumper;
use FindBin qw($Bin);
use File::Path qw(make_path remove_tree);
use File::Copy;
use File::Basename;
use lib $Bin;
use VCFComp ':all';

my $insti  = shift @ARGV;
my $srcDir = "$Bin/../samples/hapGen";
my $resDir = "$Bin/results/" . basename( $0, '.t' );

make_path($resDir);

# copy gls to res dir
my $glBase = "$resDir/simulated_gls";
my $gls    = "$glBase.bin";
copy( "$srcDir/ex.bin", $gls );

my $i2Hap = "$srcDir/ex.haps.gz";
my $i2Leg = "$srcDir/ex.leg";

my $gMap = "$srcDir/ex.map";

ok(
    system("$insti -g $gMap -C100 -m 10 -B0 -i3 -H $i2Hap -L $i2Leg -k $gls")
      == 0,
    "ran insti"
);
BGZIPandIndexSTVCFGZ("$gls.vcf.gz");

my $nrd = VCFNRD( "$gls.vcf.gz", "$srcDir/ex.vcf.gz", $resDir );
cmp_ok( $nrd, '<', 5, "simulated hap gen NRD ($nrd) < 5" );

# let's also run on the vcf version of the bin file
$gls = "$glBase.gls.vcf.gz";
copy( "$srcDir/ex.gls.vcf.gz",     $gls )       or die "could not copy";
copy( "$srcDir/ex.gls.vcf.gz.csi", "$gls.csi" ) or die "could not copy";

ok( system("$insti -g $gMap -C100 -m 10 -B0 -i3 -o $gls -Fbcf -H $i2Hap -L $i2Leg -k $gls") == 0,
    "ran insti" );
BGZIPandIndexSTVCFGZ("$gls.vcf.gz");

$nrd = VCFNRD( "$gls.vcf.gz", "$srcDir/ex.vcf.gz", $resDir );
cmp_ok( $nrd, '<', 5, "simulated hap gen NRD ($nrd) < 5" );

