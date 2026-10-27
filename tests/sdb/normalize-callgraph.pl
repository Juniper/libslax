#!/usr/bin/env perl
#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# Canonicalize libxslt's xsltSaveProfiling() callgraph report so it can
# be golden-file diffed.
#
# xsltSaveProfiling() (libbxslt/libxslt/xsltutils.c) assigns each
# template's "[N]" index, and the row/block order of both the flat
# profile and the callgraph, by sorting templates on measured wall-clock
# time.  For a fast test stylesheet the real times are near-zero and
# noisy, so ties are broken arbitrarily and the whole layout reshuffles
# from run to run, even with identical input.  Masking only the numeric
# timing columns (as CLEANPROFILE does for sdb's own per-line profiler)
# is not enough here, because the row ORDER and the "[N]" indices
# themselves are not stable.
#
# This script re-derives a stable "[N]" numbering from each template's
# name/match text (alphabetical order is as good as any; it only needs
# to be deterministic), renumbers every reference to it, sorts rows and
# callgraph blocks into that order, and replaces the timing-derived
# fields (percentage, self time, children time, the flat table's Tot and
# Avg columns) with the literal placeholder TIME.  Fields that are plain
# repeat counts (Calls, and the "x/y" called-ratios in the call graph)
# are left untouched, since those are deterministic.
#
# Usage: normalize-callgraph.pl <file>
# Rewrites <file> in place, the same convention CLEANPROFILE/CLEANADDR
# use (sed -i).

use strict;
use warnings;

my $file = shift @ARGV
    or die "usage: normalize-callgraph.pl <file>\n";

open(my $in, '<', $file) or die "cannot open $file: $!\n";
my @lines = <$in>;
close($in);
chomp @lines;

my $i = 0;
my @out;

# Pass through everything up to the flat-profile header untouched.
while ($i < @lines && $lines[$i] !~ /^number\s+match\s+name\s+mode\s+Calls/) {
    push @out, $lines[$i];
    $i++;
}
if ($i >= @lines) {
    # Not a callgraph report (e.g. "no callgraph profiling data was
    # recorded"); nothing to canonicalize.
    exit 0;
}
push @out, $lines[$i++];	# the header line itself

# Blank line(s) before the flat-profile rows.
while ($i < @lines && $lines[$i] =~ /^\s*$/) {
    push @out, $lines[$i++];
}

# Flat-profile rows: "<idx> <label> <calls> <tot> <avg>" (label may be
# "match", "name", or empty; whichever of match/name is non-empty is
# the template's display label).
my %old_to_label;
my @rows;
while ($i < @lines && $lines[$i] !~ /^\s*$/) {
    my @tok = split(' ', $lines[$i]);
    die "unexpected flat-profile row: $lines[$i]\n" if @tok < 4;
    my $calls = $tok[-3];
    my $old   = $tok[0];
    my $label = join(' ', @tok[1 .. $#tok - 3]);
    $old_to_label{$old} = $label;
    push @rows, { old => $old, label => $label, calls => $calls };
    $i++;
}

my %label_to_new;
my $n = 0;
for my $label (sort { $a cmp $b } map { $_->{label} } @rows) {
    next if exists $label_to_new{$label};
    $label_to_new{$label} = $n++;
}
my %old_to_new = map { $_ => $label_to_new{$old_to_label{$_}} } keys %old_to_label;

for my $row (sort { $label_to_new{$a->{label}} <=> $label_to_new{$b->{label}} } @rows) {
    push @out, sprintf("    %d  %-20s  calls=%s tot=TIME avg=TIME",
                        $label_to_new{$row->{label}}, $row->{label}, $row->{calls});
}

# Blank line(s), then the "Total" line: "Total <calls> <tot>".
while ($i < @lines && $lines[$i] =~ /^\s*$/) {
    push @out, $lines[$i++];
}
if ($i < @lines && $lines[$i] =~ /^\s*Total\b/) {
    my @tok = split(' ', $lines[$i]);
    my $calls = $tok[-2];
    push @out, sprintf("    Total  calls=%s tot=TIME", $calls);
    $i++;
}

# Blank line(s), then the callgraph header.
while ($i < @lines && $lines[$i] =~ /^\s*$/) {
    push @out, $lines[$i++];
}
if ($i < @lines && $lines[$i] =~ /^index\s+%\s*time/) {
    push @out, $lines[$i++];
}

# Callgraph blocks, separated by a line of dashes, up to the blank
# line before "Index by function name".
my @blocks;
my @cur;
while ($i < @lines && $lines[$i] !~ /^Index by function name/) {
    if ($lines[$i] =~ /^-+$/) {
        push @blocks, [@cur] if @cur;
        @cur = ();
    } elsif ($lines[$i] =~ /^\s*$/) {
        # blank line right before "Index by function name"; stop here
        # and let the outer loop consume it.
        last if !@cur && $i + 1 < @lines
                && $lines[$i + 1] =~ /^Index by function name/;
        push @cur, $lines[$i];
    } else {
        push @cur, $lines[$i];
    }
    $i++;
}
push @blocks, [@cur] if @cur;

sub remap_idx {
    my ($name) = @_;
    return $old_to_new{$name} if exists $old_to_new{$name};
    return $label_to_new{$name} if exists $label_to_new{$name};
    return $n;	# sentinel ("-", calls from outside the template set)
}

my @canon_blocks;
for my $block (@blocks) {
    my (@callers, @callees, $self_line);
    my $seen_self = 0;
    for my $line (@$block) {
        if ($line =~ /^\[(\d+)\]\s+(\S+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(\S+)\s+\[(\d+)\]\s*$/) {
            my ($old_idx, $pct, $self_t, $children_t, $called, $name, $idx2) =
                ($1, $2, $3, $4, $5, $6, $7);
            $self_line = {
                new_idx => remap_idx($name),
                name    => $name,
                called  => $called,
            };
            $seen_self = 1;
        } elsif ($line =~ /^\s*(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+\[(\d+)\]\s*$/) {
            my ($self_t, $children_t, $ratio, $name, $old_idx) =
                ($1, $2, $3, $4, $5);
            my $entry = { new_idx => remap_idx($name), name => $name, ratio => $ratio };
            if ($seen_self) {
                push @callees, $entry;
            } else {
                push @callers, $entry;
            }
        }
        # blank lines inside a block (shouldn't normally occur) are
        # dropped; the canonical rendering below supplies its own.
    }
    next unless $self_line;
    @callers = sort { $a->{new_idx} <=> $b->{new_idx} } @callers;
    @callees = sort { $a->{new_idx} <=> $b->{new_idx} } @callees;
    push @canon_blocks, {
        self    => $self_line,
        callers => \@callers,
        callees => \@callees,
    };
}

@canon_blocks = sort { $a->{self}{new_idx} <=> $b->{self}{new_idx} } @canon_blocks;

for my $block (@canon_blocks) {
    for my $c (@{ $block->{callers} }) {
        push @out, sprintf("        TIME TIME %-10s -> [%d] %s",
                            $c->{ratio}, $c->{new_idx}, $c->{name});
    }
    push @out, sprintf("[%d] TIME TIME TIME called=%s %s",
                        $block->{self}{new_idx}, $block->{self}{called},
                        $block->{self}{name});
    for my $c (@{ $block->{callees} }) {
        push @out, sprintf("        TIME TIME %-10s -> [%d] %s",
                            $c->{ratio}, $c->{new_idx}, $c->{name});
    }
    push @out, "-----------------------------------------------";
}

# Blank line(s), then "Index by function name" and its list.
while ($i < @lines && $lines[$i] =~ /^\s*$/) {
    push @out, $lines[$i++];
}
if ($i < @lines && $lines[$i] =~ /^Index by function name/) {
    push @out, $lines[$i++];
    my @idx_lines;
    while ($i < @lines && $lines[$i] !~ /^\s*$/) {
        if ($lines[$i] =~ /^\[(\d+)\]\s+(\S+)\s+(.*)$/) {
            my ($old_idx, $name, $rest) = ($1, $2, $3);
            push @idx_lines, { new_idx => remap_idx($name), name => $name, rest => $rest };
        }
        $i++;
    }
    for my $e (sort { $a->{new_idx} <=> $b->{new_idx} } @idx_lines) {
        push @out, sprintf("[%d] %s %s", $e->{new_idx}, $e->{name}, $e->{rest});
    }
}

# Everything else, unchanged.
while ($i < @lines) {
    push @out, $lines[$i++];
}

open(my $out, '>', $file) or die "cannot write $file: $!\n";
print $out map { "$_\n" } @out;
close($out);
