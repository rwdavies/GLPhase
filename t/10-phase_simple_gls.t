# Test file created outside of h2xs framework.
# Run this like so: `perl 10-phase_easy_gls.t'
#   wkretzsch@gmail.com     2014/12/01 14:56:15

#########################

# change 'tests => 1' to 'tests => last_test_to_print';

use Test::More;
BEGIN { plan tests => 2 }

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

my @version_nums = splice( @ARGV, 0,3);
my $insti     = shift @ARGV;
my $simpleDir = "$Bin/../samples/simple_gls";
my $resDir    = "$Bin/results/" . basename( $0, '.t' );

make_path($resDir);

# copy gls to res dir
my $simpleGLs = "$resDir/simple.gls.v1.bin";
copy( "$simpleDir/simple.gls.v1.bin", $simpleGLs );

my $gMap = "$Bin/../samples/geneticMap/genetic_map_chr20_combined_b37.txt";

ok(system("$insti $simpleGLs -g $gMap -C100 -m 100 -B0 -i10") == 0, "ran insti");
BGZIPandIndexSTBin("$simpleGLs.vcf.gz");

my $code = VCFHapMatch("$simpleGLs.vcf.gz", "$simpleDir/simple.gls.v1.expected.bin.vcf", $resDir);
ok($code eq 0, "simple haps v1") or diag($code);


#########################

# Insert your test code below, the Test::More module is used here so read
# its man page ( perldoc Test::More ) for help writing this test script.

