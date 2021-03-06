# biscuit
a little tool suite for bisulfite data

#### Install
Issue a `make` and the binary is built into `bin/`.

### Alignment of bisulfite-treated short reads
`biscuit index` and `biscuit align` adapts the `bwa index` and `bwa mem` for bisulfite-treated short reads.

#### Design
- assymmetric scoring for C to T and G to A in mapping.
- produce consistent mapping quality calculation with destination strand specification.
- produce consistent NM and MD tags under assymmetric scoring.
- produce ZR and ZC tag for retention count and conversion count
- separate seeding for parent and daughter strands for mapping efficiency
- disk space economic indices. No storage of bisulfite converted reference.
- exposes all the BWA-mem parameters to the users.
- no separate install of BWA. Dependencies easily met.
- robust to OS build, read processing and reference processing occur within computing threads of single process.
- support single and pair-end reads
- Optional parent strand and daughter strand restriction for both single-end and pair-end reads.
- Optional BSW/top/BSC/bottom strand restriction, tightly integrated in mapping.

### Somatic mutation calling
`biscuit somatic` computes somatic mutations as well as DNA methylation from matched tumor~normal pairs.

#### Feature
- supports VCF output.

### Pileup cytosine and SNPs
`biscuit pileup` computes 1) methylation (in both CpG and CpH); 2) all the callable SNP mutations. The tool is very similar in function if not superior to the well known BisSNP.

SNP calling philosophy: `pileup` tries to minimize false positive methylation call and can be stringent in terms of mutations. If there is a read mapped with high confidence and showing a mutation with high base quality score. `pileup` starts its SNP processing and MAY abolish the methylation calling if the SNP interferes the determination of cytosine retention/conversion.

#### Feature
- de novo infer bisulfite parent strand or from bsmap (ZS) or BWA-meth style tags (YD)
- call ambiguous alternative allele
- distinguish ambiguous alternative allele and multiple alternative allele
- coordinate-sorted output for sorted bam input
- flexible read filtering based on retention number, mapping quality, duplicate and mate pairing.
- flexible base filtering based on base quality, distance to the read ends.
- verbose output (cytosine context, all the bases, parent strand etc).

#### Usage

For example, to collect methylation and SNP from chr20:1256423-1257433
```Shell
pileup -r hg19.fa -i NIC1254A46.bam -q 1 -g chr20:47419734-47419734
```
outputs
```
chr20   47419733        47419734        C       .       ACG     12      0       2
```
The format reads
1) chromosome;
2) position (0-based);
3) position (1-based);
4) reference base;
5) mutation (`.` if none);
6) cytosine context (`.` if not cytosine);
7) total read coverage;
8) cytosine retention;
9) cytosine conversion count.

One might wonder why out of 12 reads coverage, one only gets 0 retention and 2 conversion. With `-v`, one may get extra information
```
chr20   47419733        47419734        C       .       ACG     12      0       2
    CCCTT   66677   ;7);<   ++---   74,68,51,44,23  21,19,21,22,18
    CCCCCCC 8888888 ;;):<<2 +++---- 69,63,63,62,59,20,1     19,19,15,17,19,26,25
```
10) base identity from bisulfite Waston strand (BSW, or C-to-T strand, `ZS:+?`, `YD:f`);
11) retention-mutation status (BSW);
12) base qualities (BSW);
13) read strand (BSW);
14) position on read (BSW);
15) number of retentions in read (BSW);
16) base identity from bisulfite Crick strand (BSC, or G-to-A strand, `ZS:-?`, `YD:r`);
17) retention-mutation status (BSC);
12) base qualities (BSC);
13) read strand (BSC);
14) position on read (BSC);
15) number of retentions in read (BSC);

One can see that in the case above, there are 5 BSW and 7 BSC, out of the 5 BSW that can inform methylation, 2 reads showing retention is of low base quality and the other is the second last base in the read (a tunable filtering option). On the contrary, 2 other reads suggesting conversion are both high qualities. Hence the output of retention and conversion count can be understood. This example shows how pileup operates on a low coverage, low quality region. One could filter based on the sum of retention and conversion to constrain the beta value computation. Note that the verbose mode `-v` also prints positions with no SNP or cytosine methylation. This allows for differentiation of "no mutation" from "no coverage".

##### code for retention-mutation status (field 11 and 17)

0: mutation to A;
1: mutation to C;
2: mutation to G;
3: mutation to T;
4: mutation to Y;
5: mutation to R;
6: retention;
7: conversion;
8: reference base;

`pileup` calls ambiguous alternative allele
```Shell
pileup -r hg19.fa -i NIC1254A46.bam -q 1 -g chr20:29570686-29570686 -v
```
outputs
```
chr20   29570685        29570686        G       G>G:9,Y:4       TCG     15      4       0
    GGTGTTTGGG      8848444888      68<<=<<<<:      --++++-++-      75,70,55,53,46,41,41,35,22,19   16,18,21,21,21,23,19,23,21,21
    GGGGG   66666   :*;<<     ++--+   62,58,55,34,30  27,24,24,22,23
```
The outputs show alternative allele Y (IUPAC code for C or T, supported by 4 reads) when BSC does not suggest alternative allele and there is equal chance of T and C (assuming no prior information of methylation and conversion ratio).

`pileup` differentiates methylation-callable and methylation-uncallable (when there is C>T or G>A mutation)
```
chr20   26138807        26138808        G       G>G:35,Y:6      TCG     45      13      5
    GGGGGGGGGGGTTGGGGTTTTGGGTG      88888888888448888444488848      :<8<<8<<<<<<:889:9<9988888      --+++++--++----++--+++-+++      61,56,53,51,40,38,37,37,27,20,20,20,19,18,14,11,10,10,10,6,6,3,3,2,1,1    4,6,6,6,8,8,8,8,8,10,10,9,9,10,10,11,11,10,10,10,10,12,12,12,11,20
    AAAGGGGGGGGGGAGGAAG     7776666666666766776     ;;;:;;;;<<<<<<78;99     +++--+---++---++---     75,70,69,67,64,63,62,61,48,45,39,27,19,13,12,6,5,4,3      9,10,10,11,12,12,13,13,13,13,13,17,15,11,16,17,15,15,18
```
and
```
chr20   25847707        25847708        G       G>G:14,Y:3      CCG     19      5       0
    GGGGGTTGGGGGGT  88888448888884  79;;:;<.<<9<8<  +++-+-+----+-+  74,70,65,59,55,54,51,50,47,40,40,36,25,14       20,20,23,19,22,19,19,13,19,21,18,22,22,12
    GGGGG   66666   9:<<:   --+-+   69,49,46,37,13  18,16,17,17,19
```
are methylation-callable. But
```
chr20   29425716        29425717        G       G>G:17,A:2      .       31      .       .
    GGTTGGGGGGGGGGGGGGAAGGG 88448888888888888800888 8<8*;::=;8<<<<<<8*;;:;/ -+--+-+--+-----+--+++-- 97,76,73,72,66,65,62,60,59,58,50,47,44,39,37,24,22,17,15,14,8,4,4 16,17,13,6,15,15,16,22,17,17,16,15,16,17,17,20,19,19,17,18,19,18,14
    GAGGAAGG        67667766        998;<;9;        --++-++-        95,72,61,56,31,27,9,9   13,12,11,8,9,10,9,9
```
is NOT methylation-callable. A "." is placed in the retention and convertion fields.