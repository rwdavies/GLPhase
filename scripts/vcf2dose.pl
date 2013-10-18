#!/usr/bin/perl -w
our $VERSION = 1.010;
$VERSION = eval $VERSION;

use threads;
use Thread::Queue 3.02;
use Thread::Semaphore;
use strict;
use 5.008;
use List::Util qw/sum/;
use Getopt::Std;
use Carp;
use Scalar::Util::Numeric qw(isint);

=head1 NAME

VCF to beagle dose format converter

=head1 SYNOPSIS

vcf2dose.pl < in.vcf > out.dose

=head1 DESCRIPTION

Takes a VCF file through stdin. Will rename ID column to
chromosome_number:position format and calculate dose for each
individual. Can handle genotype likelihoods as well.

=head2 -p [GP|GL|PL|AP|GT]    

pass in preferred field. (GP, GL, PL, AP or GT)

=head2 -u    

treat GP field as unscaled probabilities

=head2 -g

output in gprobs instead of dose format

=head2 -f [file]

specify frequencies file from which to use Hardy Weinberg 
priors to convert GLs or PLs to genotype probabilities

=head2 -h

create hard called genotypes instead of doses

=head2 -n

no sanity checking on output (speeds things up, but consider -t instead)

=head2 -t [integer]

Number of threads to run. 

=head2 -b [integer]

Max number of lines to buffer from STDIN (default is 1000);

=head1 CHANGELOG

v1.010 - added support for two column, header less frequency files

v1.009 - improved multithreading

v1.008 - added multithreading

v1.007 - added functionality for converting AP to gprobs using -g

v1.006 - added some optimizations to increase speed (-n most notably)

v1.005 - added hard call (-h) option

v1.002 - Added support for SNPtools AP field

=cut

my %args;
getopts( 'np:ugf:ht:b:d', \%args );

our $DEBUG     = $args{d};
our $gHC       = $args{h};
our $gGProbs   = $args{g};
our $gUnscaled = $args{u};

print STDERR "vcf_to_dose.pl v$VERSION\n\n";

my $cacheSize = $args{b} || 200;

# parse freq file
my $hVaraf;
if ( $args{f} ) {
    $hVaraf = getVarAFs( f => $args{f} );
}

my $numThreads = $args{t} || 1;
$numThreads = 1 unless $numThreads =~ m/^\d+$/;
croak "-t option must be an integer greater 0" unless $numThreads > 0;
print STDERR "Running in multithreaded mode with $numThreads thread(s).\n";

=head1 FORMAT Fields Used

included: true if the flag is contained in the bam header
field: location of flag info in FORMAT field

fields are ordered by preference GP > GL > PL > AP > GT

=cut

# determine preferred field
my @ordered_fields  = qw/GP AP GT GL PL/;
my %supported_flags = (
    GP => { included => 0, field => undef }
    ,    # genotype probability (phred scaled)
    GL => { included => 0, field => undef }
    ,    # genotype likelihood (log10 scaled)
    PL => { included => 0, field => undef }
    ,    # genotype likelihood (rounded, phred scaled)
    AP => { included => 0, field => undef },    # allelic probability
    GT => { included => 0, field => undef },    # genotype
);

my $preferredField = $args{p};
if ( defined $preferredField ) {
    confess "need to specify valid preferred field"
      unless defined $supported_flags{$preferredField};
    unshift @ordered_fields, $preferredField;
}

# skipping comments
my $new_line = 1;
while ($new_line) {
    $new_line = <>;
    if ( $new_line =~ m/^##/ ) {

        # parse ID tags to search for
        # only register flags we are looking for
        $new_line =~ m/^##FORMAT=\<ID=([a-zA-Z0-9]+)/;
        my $match = $1;
        if ( defined $match && defined $supported_flags{$match} ) {
            $supported_flags{$match}->{included} = 1;
        }
    }
    else {
        last;
    }
}

# determine which information to use for dose estimate
my $preferred_id = undef;
FIELD_ID: for my $fieldID (@ordered_fields) {
    if ( $supported_flags{$fieldID}->{included} ) {
        $preferred_id = $fieldID;
        last FIELD_ID;
    }
}

confess "no field necessary for dose estimation is contained in input file"
  unless defined $preferred_id;

# tell user what we're doing
if ( $args{g} ) {
    print STDERR "Creating GLs in gprobs format from $preferred_id field\n";
}
else {
    print STDERR "Creating dose estimate from $preferred_id field\n";
}
print STDERR "Outputting hard calls\n" if $args{h};
print STDERR "No sanity checks\n"      if $args{n};

# print header
chomp($new_line);
my @header_line = split( /\t/, $new_line );
print STDOUT 'marker alleleA alleleB ';

if ( $args{g} ) {
    print STDOUT
      join( ' ', map { ($_) x 3 } @header_line[ 9 .. $#header_line ] ) . "\n";
}
else {
    print STDOUT join( ' ', @header_line[ 9 .. $#header_line ] ) . "\n";
}

# to guarantee serialized output
my $sSTDOUT = Thread::Semaphore->new();

# figure out format ordering and print first line
$new_line = <>;
my $raOut = [];
my ( $chromLoc, $raLine ) = preProcessLine( $new_line, $raOut );
my @format = split( /\:/, $raLine->[8] );
foreach my $field_num ( 0 .. $#format ) {
    my $fieldID = $format[$field_num];
    if ( defined $supported_flags{$fieldID} ) {

        # check for missing header
        if ( !$supported_flags{$fieldID}->{included} ) {
            confess "$fieldID does not have a header line";
        }

        # save location of field
        $supported_flags{$fieldID}->{field} = $field_num;
    }
}

# create input queue
my $qLines = Thread::Queue->new();
my $qOut   = Thread::Queue->new();
processIndLine( $raLine, $supported_flags{$preferred_id}->{field},
    \%args, $sSTDOUT, $raOut, $qOut, 1 );

# kick off worker threads
my @threads;
for ( 1 .. $numThreads ) {
    push @threads,
      threads->create( 'processLinesWorker', \%supported_flags, $sSTDOUT,
        $qLines, \%args, $qOut, $preferred_id );
}

my $inLineNum  = 1;
my $outLineNum = 0;

# end qOut to tell empty outline worker when to return
my $qInLineNum  = Thread::Queue->new();
my $qOutLineNum = Thread::Queue->new();
$qInLineNum->enqueue($inLineNum);    # just to get started on the first line
my $tEmptyOutLineWorker =
  threads->create( 'emptyOutInOrderWorker', $qOut, $qInLineNum, $qOutLineNum );

# print body
while ( $new_line = <> ) {
    $inLineNum++;

    # Only read next line in to queue if there are less lines in cache
    # than the cache size parameter
    # Otherwise sleep for a second and give an update of how far we are
    while ( $inLineNum % $cacheSize == 0 && $qLines->pending() > $cacheSize ) {
        sleep 1;

        # check status of jobs if we are waiting anyway
        #defined( my $nextOutLineNum = $qOutLineNum->dequeue() )
        if ( my $numPending = $qOutLineNum->pending() ) {
            my @nextOutLineNums = $qOutLineNum->dequeue($numPending);
            $outLineNum = pop @nextOutLineNums;
            print STDERR "Processing line number $outLineNum/$inLineNum\n";
        }
    }
    $qLines->enqueue( $inLineNum, $new_line );
    $qInLineNum->enqueue($inLineNum);
}

# finish input and print queues
$qLines->end();
$qInLineNum->end();

# continue to check on status
while ( $outLineNum < $inLineNum ) {

    sleep 1;
    if ( my $numPending = $qOutLineNum->pending() ) {
        my @nextOutLineNums = $qOutLineNum->dequeue($numPending);
        $outLineNum = pop @nextOutLineNums;
        print STDERR "Processing line number $outLineNum/$inLineNum\n";
    }
}

# wait for all threads to finish
print STDERR "Waiting on worker queues to finish...";
map { $_->join() } @threads;
$tEmptyOutLineWorker->join();
print STDERR "done.\n";

exit 0;

sub emptyOutInOrderWorker {
    my ( $qOut, $qInLineNum, $qOutLineNum ) = @_;

    my %cache;
    my $outLineNum = 0;
    my $inLineNum  = 1;
    while ( defined( my $nextInLineNum = $qInLineNum->dequeue() ) ) {
        $inLineNum = $nextInLineNum;
        $outLineNum = emptyOutInOrder( $qOut, $outLineNum, \%cache );
        $qOutLineNum->enqueue($outLineNum);
    }
    while ( $outLineNum < $inLineNum ) {
        $outLineNum = emptyOutInOrder( $qOut, $outLineNum, \%cache );
        $qOutLineNum->enqueue($outLineNum);
        sleep 1;
    }
    $qOutLineNum->end();
    croak "Only $outLineNum out of $inLineNum lines printed"
      if $outLineNum != $inLineNum;
    print STDERR "\nemptyOutInOrderWorker returning...\n" if $DEBUG;
}

sub emptyOutInOrder {
    my $qOut        = shift;
    my $outLineNum  = shift;
    my $rhLineCache = shift;

    while ( $qOut->pending() > 1 ) {
        my ( $lineNum, $out ) = $qOut->dequeue(2);

        # this is the next line to print
        if ( $lineNum + 1 == $outLineNum ) {
            print STDOUT $out;
            $outLineNum++;
        }

        # otherwise put the line in cache
        else {
            $rhLineCache->{$lineNum} = $out;
        }
    }

    # check cache for line with the correct line number
  KEY: for my $key ( sort keys %{$rhLineCache} ) {
        my $searchLineNum = $outLineNum + 1;

        # if the correct line number exists,
        # then pull it out of cache, print it and go to the next key
        if ( exists $rhLineCache->{$searchLineNum} ) {
            print STDOUT $rhLineCache->{$searchLineNum};
            delete $rhLineCache->{$searchLineNum};
            $outLineNum++;
        }

        # if the current key was not the searched for one
        # then none of the subsequent keys will be either.
        else {
            last KEY;
        }
    }

    return $outLineNum;
}

sub processLinesWorker {

    my $rhFlags      = shift;
    my $sSTDOUT      = shift;
    my $qLines       = shift;
    my $rhArgs       = shift;
    my $qOut         = shift;
    my $preferred_id = shift;

    my ( $lineNum, $new_line ) = $qLines->dequeue(2);
    while ( defined $lineNum ) {
        my $raOut = [];
        my ( $chromLoc, $raLine ) = preProcessLine( $new_line, $raOut );
        processIndLine( $raLine, $rhFlags->{$preferred_id}->{field},
            $rhArgs, $sSTDOUT, $raOut, $qOut, $lineNum );
        ( $lineNum, $new_line ) = $qLines->dequeue(2);
    }
    print STDERR "\nprocessLinesWorker returning...\n" if $DEBUG;
}

sub preProcessLine {

    my $new_line = shift;
    my $raOut    = shift;

    chomp($new_line);
    my @line = split( /\t/, $new_line );
    my $chromLoc = join( q/:/, @line[ 0 .. 1 ] );

    push @{$raOut}, $chromLoc . ' ' . join( q/ /, @line[ 3 .. 4 ] );

    return ( $chromLoc, \@line );
}

sub processIndLine {

    my $raLine   = shift;
    my $fieldNum = shift;
    my $rhArgs   = shift;
    my $sSTDOUT  = shift;
    my $raOut    = shift;
    my $qOut     = shift;
    my $lineNum  = shift;

    my ( $args_f, $args_n, $args_h ) =
      ( $rhArgs->{f}, $rhArgs->{n}, $rhArgs->{h} );

    foreach my $col ( @{$raLine}[ 9 .. $#{$raLine} ] ) {

        my @print_val;

        my @col = split( /\:/, $col );
        my $used_col = $col[$fieldNum];
        unless ( defined $used_col ) {
            confess '$used_col is not defined from line:\n' . "@col\n";
        }

        # convert input fields to something 0:1 scaled (like, prob, or genotype)
        my @genotype_probs = get_geno_probs( $preferred_id, $used_col );

        # use hardy weinberg prior if freqs file given
        if ($args_f) {
            @genotype_probs = GLToGP(
                prefID   => $preferred_id,
                likes    => \@genotype_probs,
                varafs   => $hVaraf,
                chromLoc => $chromLoc,
            );
        }

        # turn into genotype or dose, or keep probs if -g
        @print_val = toDose( $preferred_id, @genotype_probs );

        unless ($args_n) {

            # sanity check: are all vals between 0 and 2?
            foreach my $val_num ( 0 .. $#print_val ) {
                unless ( $print_val[$val_num] eq '.' ) {
                    my $val = $print_val[$val_num];
                    $print_val[$val_num] = sprintf( "%.4f", $val );
                    if ( $val > 2 || $val < 0 ) {
                        confess "unexpected output: $val at line $lineNum";
                    }
                }
            }

            if ($args_h) {

                # sanity check: there should be only one print val
                confess
"unexpected output: more than one print val ( @print_val ) at line $lineNum"
                  if @print_val != 1;

                # sanity check: the print val should be integer
                confess
"unexpected output: print val is not an integer ( @print_val ) at line $lineNum"
                  if $print_val[0] != sprintf( '%u', $print_val[0] );

            }
        }

        if ($args_h) {
            push @{$raOut}, q/ / . sprintf( '%u', $print_val[0] );
        }
        else {
            push @{$raOut}, q/ / . join( ' ', @print_val );
        }

    }
    $sSTDOUT->down();
    $qOut->enqueue( $lineNum, join( '', @{$raOut} ) . "\n" );
    $sSTDOUT->up();
}

sub getVarAFs {

    my %args = @_;
    confess "need to define freqs file with -f" unless defined $args{f};

    # read culled freqs file
    my $fh;

    if ( $args{f} =~ m/\.gz$/ ) {
        open( $fh, "gzip -dc $args{f} |" ) or die "could not open $args{f}";
    }
    else {
        open( $fh, "<", $args{f} ) or die "could not open $args{f}";
    }

    # parse each line and save to hash
    my %varaf;
    my $numCol = undef;
    while (<$fh>) {
        chomp;

        # checking header
        if (! defined $numCol) {

              if ( $_ eq "CHROM\tPOS\talleleA\talleleB\tFreqA\tFreqB" ) {
                $numCol    = 6;
                next;
            }
            elsif (m/^\S+ \S+$/) {
                $numCol    = 2;
            }
            else {
                confess "malformed header in $args{f}";
            }
        }

        my @line;
        if ( $numCol == 6 ) {
            @line = split(/\t/);
            my $key = join( ':', @line[ 0 .. 1 ] );
            $varaf{$key} = $line[5];
        }
        elsif ( $numCol == 2 ) {
            @line = split(/ /);
            $varaf{ $line[0] } = $line[1];
        }

    }
    close($fh);

    return \%varaf;
}

# convert GLs to genotype probs if freqs file given
sub GLToGP {
    my %args = @_;

    confess "need to define preferred ID" unless defined $args{prefID};
    confess "need to define chromLoc"     unless defined $args{chromLoc};
    confess "need to define genotype likes"
      unless ref $args{likes} eq 'ARRAY';
    confess "need to pass in VarAFs" unless ref $args{varafs} eq 'HASH';

    # only run if using GLs or PLs
    confess
"multiplying non-genotype likelihood with hardy-weinberg probably makes no sense. Exiting"
      unless $args{prefID} =~ m/GL|PL/;

    # compute HW priors
    my $varaf = $args{varafs}->{ $args{chromLoc} }
      || confess "$args{chromLoc} does not exist in freq file";
    my @hardyWeinbergPriors = (
        ( 1 - $varaf ) * ( 1 - $varaf ),
        2 * ( $varaf * ( 1 - $varaf ) ),
        $varaf * $varaf
    );

    # convert to probs
    my @likes = @{ $args{likes} };
    my @probs = ();
    for my $idx ( 0 .. 2 ) {
        $probs[$idx] = $likes[$idx] * $hardyWeinbergPriors[$idx];
    }
    return @probs;
}

sub AP2GP {
    my ( $a1, $a2 ) = @_;
    return (
        ( 1 - $a1 ) * ( 1 - $a2 ),
        ( $a1 * ( 1 - $a2 ) + $a2 * ( 1 - $a1 ) ),
        ( $a1 * $a2 )
    );
}

# convert to dose if not GT field used
sub toDose {

#    confess "need to define preferred ID" unless defined $args{prefID};
#    confess "need to define genotype probs" unless ref $args{probs} eq 'ARRAY';

    my $preferred_id = shift;
    my @gprobs       = @_;

    my @print_val;

    # don't change anything if GT field used
    if ($gGProbs) {
        if ( $preferred_id eq 'GT' ) {
            confess "-g option can't work with GT field";
        }
        elsif ( $preferred_id eq 'AP' ) {
            @print_val = AP2GP(@gprobs);
        }
        else {
            @print_val = @gprobs;
        }
    }

    # convert from prob/like to dose otherwise
    else {

        if ( $preferred_id eq 'AP' ) {
            if ($gHC) {

                # convert AP to GP
                my @rGProbs = AP2GP(@gprobs);
                my $idxMax  = 0;
                $rGProbs[$idxMax] > $rGProbs[$_]
                  or $idxMax = $_
                  for 1 .. $#rGProbs;
                @print_val = ($idxMax);
            }
            else {
                @print_val = ( sum(@gprobs) );
            }
        }
        elsif ( $preferred_id eq 'GT' ) {
            @print_val = @gprobs;
        }
        elsif ( $preferred_id =~ m/^(GP|GL|PL)$/ ) {
            if ($gHC) {
                my $idxMax = 0;
                $gprobs[$idxMax] > $gprobs[$_]
                  or $idxMax = $_
                  for 1 .. $#gprobs;
                @print_val = ($idxMax);
            }
            else {
                @print_val = ( ( $gprobs[1] + $gprobs[2] * 2 ) / sum(@gprobs) );
            }
        }
        else {
            confess "could not figure out what to do with field $preferred_id";
        }

    }
    return @print_val;

}

# return data as genotype probs/likes scaled [0,1]
sub get_geno_probs {

    #    confess "need to define preferred ID" unless defined $args{prefID};
    #    confess "need to define used col"     unless defined $args{usedCol};

    my $preferred_id = shift;
    my $used_col     = shift;

    my @print_val;
    if ( $preferred_id eq 'GP' ) {
        my @genotype_phred_probability = split( /\,/, $used_col );
        my @genotype_probability;

        if ($gUnscaled) {
            @genotype_probability = @genotype_phred_probability;
        }
        else {
            @genotype_probability =
              map { 10**( $_ / -10 ) } @genotype_phred_probability;
        }

        @print_val = @genotype_probability;
    }
    elsif ( $preferred_id eq 'GL' ) {
        my @genotype_log_likelihoods = split( /\,/, $used_col );
        my @genotype_likelihoods =
          map { 10**($_) } @genotype_log_likelihoods;

        @print_val = @genotype_likelihoods;
    }
    elsif ( $preferred_id eq 'PL' ) {
        my @genotype_phred_likelihoods = split( /\,/, $used_col );
        my @genotype_likelihoods =
          map { 10**( $_ / -10 ) } @genotype_phred_likelihoods;

        @print_val = @genotype_likelihoods;
    }
    elsif ( $preferred_id eq 'AP' ) {
        @print_val = split( /\,/, $used_col );
    }
    elsif ( $preferred_id eq 'GT' ) {

        # check for missing data in 'GT' field
        if ( $used_col =~ /\./ ) {
            @print_val = qw'.';
        }
        else {
            #                print STDERR "$lineNum\t$used_col\n";
            my @alleles = split( qr([\|\/]), $used_col );
            unless ( defined $alleles[1] ) {
                @print_val = qw/./;
            }
            else {
                @print_val = ( $alleles[0] + $alleles[1] );
            }
        }
    }
    else {
        confess "could not figure out what to do with field $preferred_id";
    }

    return @print_val;
}

