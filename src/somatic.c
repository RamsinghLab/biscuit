#include "pileup.h"

typedef struct {
  double mu_somatic;
  conf_t c;
} sconf_t;

/* typedef enum {BSS_RETENTION, BSS_CONVERSION, BSS_OTHER} bsstate_t; */
/* typedef enum {MCT, MCG, MCA, MGT, MGC, MGA} mutation_t; */
/* const char alts[] = "TGATCA"; */

typedef struct {
  int n;                        /* number of sites */
  pileup_data_v **tumo_data;
  pileup_data_v **norm_data;
} spileup_t;

spileup_t *init_spileup(int n) {
  spileup_t *p = malloc(sizeof(spileup_t));
  p->n = n;
  p->tumo_data = calloc(n, sizeof(pileup_data_v*));
  p->norm_data = calloc(n, sizeof(pileup_data_v*));
  return p;
}

void destroy_spileup(spileup_t *p) {
  int i;
  for (i=0; i<p->n; ++i) {
    if (p->tumo_data[i]) free_pileup_data_v(p->tumo_data[i]);
    if (p->norm_data[i]) free_pileup_data_v(p->norm_data[i]);
  }
  free(p->tumo_data);
  free(p->norm_data);
  free(p);
}

typedef struct {
  char *tumo_fn;                /* on stack */
  char *norm_fn;                /* on stack */
  char *ref_fn;                 /* on stack */
  wqueue_t(window) *q;
  wqueue_t(record) *rq;
  sconf_t *conf;
} result_t;

static void verbose_format(char m, uint8_t bsstrand, pileup_data_v *dv, kstring_t *s) {

  if (!dv) return;
  uint32_t i, nf;

  /* return if no record match the bsstrand */
  int n=0;
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) ++n;
  }
  if (!n) return;

  char b='0'+bsstrand;

  /* 1. base */
  ksprintf(s, ";Bs%c%c=", m, b);
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) kputc(d->qb, s);
  }

  /* 2. status array */
  ksprintf(s, ";Sta%c%c=", m, b);
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) kputc('0'+d->stat, s);
  }

  /* 3. base quality */
  ksprintf(s, ";Bq%c%c=", m, b);
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) kputc(d->qual+33, s);
  }

  /* 4. strand */
  ksprintf(s, ";Str%c%c=", m, b);
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) kputc(d->strand?'-':'+', s);
  }

  /* 5. position on read */
  ksprintf(s, ";Pos%c%c=", m, b);
  nf = 0;
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) {
      if (nf) kputc(',', s);
      else nf = 1;
      kputuw(d->qpos, s);
    }
  }

  /* 6. retention count 
     retention count, for diagnosing incomplete converted
     reads from CpH sites and mitochondrial sites */
  ksprintf(s, ";Rret%c%c=", m, b);
  nf = 0;
  for (i=0; i<dv->size; ++i) {
    pileup_data_t *d = ref_pileup_data_v(dv,i);
    if (d->bsstrand == bsstrand) {
      if (nf) kputc(',', s);
      else nf = 1;
      kputuw(d->cnt_ret, s);
    }
  }
}

static void plp_format(refseq_t *rs, char *chrm, uint32_t rpos,
                       pileup_data_v *dv_t, pileup_data_v *dv_n, sconf_t *conf, kstring_t *s) {
  uint32_t i;
  char rb = toupper(getbase_refseq(rs, rpos));

  int cnts_t[9] = {0};
  int cnts_n[9] = {0};
  int cm1_t, cm2_t, cm1_n, cm2_n;
  plp_getcnts(dv_t, &conf->c, cnts_t, &cm1_t, &cm2_t);
  plp_getcnts(dv_n, &conf->c, cnts_n, &cm1_n, &cm2_n);

  /* if not SNP but no signal for BSS_RETENTION or BSS_CONVERSION,
     skip the print when in non-verbose mode */
  if (cm1_t < 0 && cm1_n < 0 && !conf->c.verbose
      && cnts_t[BSS_RETENTION] == 0
      && cnts_t[BSS_CONVERSION] == 0
      && cnts_n[BSS_RETENTION] == 0
      && cnts_n[BSS_CONVERSION] == 0) return;

  /* MY and MR do not interfere */
  uint8_t methcallable_t=0;
  uint8_t methcallable_n=0;
  if (cnts_t[BSS_RETENTION] + cnts_t[BSS_CONVERSION] > 0) {
    if (dv_t && cnts_t[BSS_MT]==0 && rb == 'C') methcallable_t = 1;
    if (dv_t && cnts_t[BSS_MA]==0 && rb == 'G') methcallable_t = 1;
  }
  if (cnts_n[BSS_RETENTION] + cnts_n[BSS_CONVERSION] > 0) {
    if (dv_n && cnts_n[BSS_MT]==0 && rb == 'C') methcallable_n = 1;
    if (dv_n && cnts_n[BSS_MA]==0 && rb == 'G') methcallable_n = 1;
  }

  ksprintf(s, "%s\t%u\t.\t%c\t", chrm, rpos, rb);

  /* ALT */
  if (cm1_t >= 0 || cm1_n >= 0) {
    uint32_t supp[6];
    for (i=0; i<6; ++i) supp[i] = ((cnts_t[i] + cnts_n[i])<<4) | i;
    qsort(supp, 6, sizeof(uint32_t), compare_supp);
    int fst = 1;
    for (i=0; i<6; ++i) {
      if ((supp[i]>>4) > 0) {
        char m = nt256int8_to_mutcode[supp[i]&0xf];
        if (m != rb) {
          if (!fst) kputc(',', s);
          kputc(m, s);
          fst = 0;
        }
      }
    }
  } else {
    kputc('.', s);
  }

  char gt[4] = "./.";
  int cref_n = reference_supp(cnts_n);
  int cref_t = reference_supp(cnts_t);
  int altsupp_n = cm1_n >=0 ? cnts_n[cm1_n] : 0;
  
  double gl0, gl1, gl2, gq=-1;
  pileup_genotype(cref_n, altsupp_n, &conf->c, gt, &gl0, &gl1, &gl2, &gq);

  /* QUAL */
  ksprintf(s, "\t%1.2f", gq);
  if (gq > 1) {
    kputs("\tPASS\t", s);
  } else {
    kputs("\tLowQual\t", s);
  }

  /* INFO tags */
  if (dv_t) ksprintf(s, "DPT=%u", dv_t->size);
  else kputs("DPT=0", s);
  if (dv_n) ksprintf(s, ";DPN=%u", dv_n->size);
  else kputs(";DPN=0", s);

  if (methcallable_t || methcallable_n)
    fivenuc_context(rs, rpos, s, rb);

  if (methcallable_t) ksprintf(s, ";RetnT=%d;ConvT=%d", cnts_t[BSS_RETENTION], cnts_t[BSS_CONVERSION]);
  if (methcallable_n) ksprintf(s, ";RetnN=%d;ConvN=%d", cnts_n[BSS_RETENTION], cnts_n[BSS_CONVERSION]);

  if (cref_t || cm1_t >=0) {
    kputs(";SPT=", s);
    allele_supp(rb, cref_t, cm1_t, cm2_t, cnts_t, s);
  }
  if (cref_n || cm1_n >=0) {
    kputs(";SPN=", s);
    allele_supp(rb, cref_n, cm1_n, cm2_n, cnts_n, s);
  }

  /* additional information printed on verbose */
  if (conf->c.verbose) {
    verbose_format('T', 0, dv_t, s);
    verbose_format('T', 1, dv_t, s);
    verbose_format('N', 0, dv_n, s);
    verbose_format('N', 1, dv_n, s);
  }

  kputs("\tGT:GP:GQ:SST:SSC", s);
  if (gq>0) {
    ksprintf(s, "\t%s:%1.0f,%1.0f,%1.0f:%1.2f", gt, min(1000, -gl0), min(1000, -gl1), min(1000, -gl2), gq);
  } else {
    ksprintf(s, "\t./.:.:.");
  }

  double squal;
  if (dv_n && dv_t) {
    int altcnt_t = 0;
    int altcnt_n = 0;
    if (cm1_t > 0) {
      altcnt_t = cnts_t[cm1_t];
      altcnt_n = cnts_n[cm1_t];
    }
    squal = pval2qual(somatic_posterior(cref_t, altcnt_t, cref_n, altcnt_n,
                                        conf->c.error, conf->c.mu, conf->mu_somatic, conf->c.contam));
    if (squal > 1) {
      ksprintf(s, ":1:%1.1f", squal);
    } else {
      ksprintf(s, ":0:%1.1f", squal);
    }
  } else {
    kputs(":0:2", s);
  }
  kputc('\n', s);
}

void pileup_window(pileup_data_v **pdata, window_t *w, bam_iter_t iter, samfile_t *sam, refseq_t *rs, sconf_t *conf) {

  bam1_t *b = bam_init1();
  int ret;
  char qb, rb;
  while ((ret = bam_iter_read(sam->x.bam, iter, b))>0) {

    uint8_t bsstrand = get_bsstrand(rs, b, conf->c.min_base_qual);

    bam1_core_t *c = &b->core;

    /* read-based filtering */
    if (c->qual < conf->c.min_mapq) continue;
    if (c->l_qseq < 0 || (unsigned) c->l_qseq < conf->c.min_read_len) continue;
    if (c->flag > 0){         /* only when any flag is set */
      if (conf->c.filter_secondary && c->flag & BAM_FSECONDARY) continue;
      if (conf->c.filter_duplicate && c->flag & BAM_FDUP) continue;
      if (conf->c.filter_ppair && !(c->flag & BAM_FPROPER_PAIR)) continue;
      if (conf->c.filter_qcfail && c->flag & BAM_FQCFAIL) continue;
    }
      
    uint32_t rpos = c->pos+1, qpos = 0;
    uint8_t *nm = bam_aux_get(b, "NM");
    if (nm && bam_aux2i(nm)>conf->c.max_nm) continue;
    uint32_t cnt_ret = cnt_retention(rs, b, bsstrand);
    if (cnt_ret > conf->c.max_retention) continue;

    rpos = c->pos+1; qpos = 0;
    int i;
    uint32_t j;
    for (i=0; i<c->n_cigar; ++i) {
      uint32_t op = bam_cigar_op(bam1_cigar(b)[i]);
      uint32_t oplen = bam_cigar_oplen(bam1_cigar(b)[i]);
      switch(op) {
      case BAM_CMATCH:
        for (j=0; j<oplen; ++j) {
          if (rpos+j<w->beg || rpos+j>=w->end) continue; /* include begin but not end */
          rb = toupper(getbase_refseq(rs, rpos+j));
          /* if (rb != 'C' && rb != 'G') continue; */
          qb = bscall(b, qpos+j);
          pileup_data_v **plp_data_vec = pdata+rpos+j-w->beg;
          if (!*plp_data_vec) *plp_data_vec = init_pileup_data_v(2);
          pileup_data_t *d = next_ref_pileup_data_v(*plp_data_vec);
          d->qual = bam1_qual(b)[qpos+j];
          d->cnt_ret = (unsigned) cnt_ret;
          d->strand = (c->flag&BAM_FREVERSE)?1:0;
          d->qpos = qpos+j+1;
          d->rlen = c->l_qseq;
          d->bsstrand = bsstrand;
          d->qb = qb;

          if (bsstrand) {          /* BSC */
            if (rb == 'G') {
              if (qb == 'A') d->stat = BSS_CONVERSION;
              else if (qb == 'G') d->stat = BSS_RETENTION;
              else d->stat = mutcode(qb);
            } else if (rb != qb) {
              if (qb == 'A') d->stat = BSS_MR;
              else d->stat = mutcode(qb);
            } else {
              d->stat = BSS_N;
            }
      
          } else {                    /* BSW */
            if (rb == 'C') {
              if (qb == 'T') d->stat = BSS_CONVERSION;
              else if (qb == 'C') d->stat = BSS_RETENTION;
              else d->stat = mutcode(qb);
            } else if (rb != qb) {
              if (qb == 'T') d->stat = BSS_MY;
              else d->stat = mutcode(qb);
            } else {
              d->stat = BSS_N;
            }
          }

        }
        rpos += oplen;
        qpos += oplen;
        break;
      case BAM_CINS:
        qpos += oplen;
        break;
      case BAM_CDEL:
        rpos += oplen;
        break;
      case BAM_CSOFT_CLIP:
        qpos += oplen;
        break;
      case BAM_CHARD_CLIP:
        qpos += oplen;
        break;
      default:
        fprintf(stderr, "Unknown cigar, %u\n", op);
        abort();
      }
    }
  }
  bam_destroy1(b);
}


static void *process_func(void *data) {

  result_t *res = (result_t*) data;
  sconf_t *conf = (sconf_t*) res->conf;
  samfile_t *tumo_sam = samopen(res->tumo_fn, "rb", 0);
  samfile_t *norm_sam = samopen(res->norm_fn, "rb", 0);
  bam_index_t *tumo_idx = bam_index_load(res->tumo_fn);
  bam_index_t *norm_idx = bam_index_load(res->norm_fn);
  refseq_t *rs = init_refseq(res->ref_fn, 1000, 1000);

  char rb;
  record_t rec;
  window_t w;
  uint32_t j;
  while (1) {

    wqueue_get(window, res->q, &w);
    if (w.tid == -1) break;

    spileup_t *plp = init_spileup(w.end - w.beg);
    
    char *chrm = tumo_sam->header->target_name[w.tid];
    fetch_refseq(rs, chrm, w.beg>100?w.beg-100:1, w.end+100);
    bam_iter_t tumo_iter = bam_iter_query(tumo_idx, w.tid, w.beg>1?(w.beg-1):1, w.end);
    bam_iter_t norm_iter = bam_iter_query(norm_idx, w.tid, w.beg>1?(w.beg-1):1, w.end);

    pileup_window(plp->tumo_data, &w, tumo_iter, tumo_sam, rs, conf);
    pileup_window(plp->norm_data, &w, norm_iter, norm_sam, rs, conf);

    /* run through cytosines */
    rec.s.l = rec.s.m = 0; rec.s.s = 0;
    rec.block_id = w.block_id;
    for (j=w.beg; j<w.end; ++j) {
      rb = getbase_refseq(rs, j);
      pileup_data_v *plp_tumo_data = plp->tumo_data[j-w.beg];
      pileup_data_v *plp_norm_data = plp->norm_data[j-w.beg];
      if (plp_tumo_data || plp_norm_data) {
        plp_format(rs, chrm, j, plp_tumo_data, plp_norm_data, res->conf, &rec.s);
      }
    }
    wqueue_put2(record, res->rq, rec);
    destroy_spileup(plp);
    bam_iter_destroy(tumo_iter);
    bam_iter_destroy(norm_iter);
  }
  free_refseq(rs);
  samclose(tumo_sam);
  samclose(norm_sam);
  bam_index_destroy(tumo_idx);
  bam_index_destroy(norm_idx);
  return 0;
}

static void head_append_verbose(char *pc, char *pb, char m, char b, kstring_t *s) {

  ksprintf(s, "##INFO=<ID=Bs%c%c,Number=1,Type=String,Description=\"base identity, %s, %s\">\n", m, b, pc, pb);
  ksprintf(s, "##INFO=<ID=Sta%c%c,Number=1,Type=String,Description=\"Status code, %s, %s (0,1,2,3 for mutation into A,C,G,T; 4,5 for Y,R; 6,7 for retention and conversion; 8 for other normal;)\">\n", m, b, pc, pb);
  ksprintf(s, "##INFO=<ID=Bq%c%c,Number=1,Type=String,Description=\"base quality, %s, %s\">\n", m, b, pc, pb);
  ksprintf(s, "##INFO=<ID=Str%c%c,Number=1,Type=String;Description=\"strands, %s, %s\">\n", m, b, pc, pb);
  ksprintf(s, "##INFO=<ID=Pos%c%c,Number=1,Type=String;Description=\"position in read, %s, %s\">\n", m, b, pc, pb);
  ksprintf(s, "##INFO=<ID=Rret%c%c,Number=1,Type=String;Description=\"Number of retention in read, %s, %s\">\n", m, b, pc, pb);
}

static void head_append_info(char *p, char m, kstring_t *s) {
  ksprintf(s, "##INFO=<ID=DP%c,Number=1,Type=Integer,Description=\"Raw read depth, %s\">\n", m, p);
  ksprintf(s, "##INFO=<ID=SP%c,Number=1,Type=String,Description=\"Allele support, %s (with filtering)\">\n", m, p);
  ksprintf(s, "##INFO=<ID=Retn%c,Number=1,Type=String,Description=\"Retention count, %s (with filtering)\">\n", m, p);
  ksprintf(s, "##INFO=<ID=Conv%c,Number=1,Type=String,Description=\"Conversion count, %s (with filtering)\">\n", m, p);
}

static int usage() {
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: somatic [options] -r [ref.fa] -i [tumor.bam] -j [normal.bam] -o [out.pileup] -g [chr1:123-234]\n");
  fprintf(stderr, "Input options:\n\n");
  fprintf(stderr, "     -i        tumor bam.\n");
  fprintf(stderr, "     -j        normal bam.\n");
  fprintf(stderr, "     -r        reference in fasta.\n");
  fprintf(stderr, "     -g        region (optional, if not specified the whole bam will be processed).\n");
  fprintf(stderr, "     -s        step of window dispatching [100000].\n");
  fprintf(stderr, "     -q        number of threads [3] recommend 20.\n");
  fprintf(stderr, "\nOutputing format:\n\n");
  fprintf(stderr, "     -H        no header\n");
  fprintf(stderr, "     -o        pileup output file\n");
  fprintf(stderr, "     -v        verbose (print additional info for diagnosis).\n");
  fprintf(stderr, "\nGenotyping parameters:\n\n");
  fprintf(stderr, "     -E        error rate [1e-3].\n");
  fprintf(stderr, "     -M        mutation rate [1e-3].\n");
  fprintf(stderr, "     -S        somatic mutation rate [1e-3].\n");
  fprintf(stderr, "     -C        contamination rate [5e-3].\n");
  fprintf(stderr, "     -P        prior probability for heterozygous variant [1/3].\n");
  fprintf(stderr, "     -Q        prior probability for homozygous variant [1/3].\n");
  fprintf(stderr, "\nPileup filtering:\n\n");
  fprintf(stderr, "     -b        min base quality [20].\n");
  fprintf(stderr, "     -m        minimum mapping quality [40].\n");
  fprintf(stderr, "     -t        max retention in a read [999999].\n");
  fprintf(stderr, "     -l        minimum read length [10].\n");
  fprintf(stderr, "     -e        minimum distance to end of a read [3].\n");
  fprintf(stderr, "     -c        NO filtering secondary mapping.\n");
  fprintf(stderr, "     -u        NO filtering of duplicate.\n");
  fprintf(stderr, "     -p        NO filtering of improper pair (!BAM_FPROPER_PAIR).\n");
  fprintf(stderr, "     -n        maximum NM tag [255].\n");
  fprintf(stderr, "     -h        this help.\n");
  fprintf(stderr, "\n");
  return 1;
}

int main_somatic(int argc, char *argv[]) {

  int c;
  char *reffn = 0;
  char *reg = 0;
  char *tumo_fn = 0;
  char *norm_fn = 0;
  char *outfn = 0;
  sconf_t conf = {
    .mu_somatic = 0.001,
  };
  conf_init(&conf.c);

  if (argc<2) return usage();
  while ((c=getopt(argc, argv, "E:M:S:C:P:Q:i:j:o:r:s:g:q:e:b:t:n:m:l:Hcupvh"))>=0) {
    switch (c) {
    case 'E': conf.c.error = atof(optarg); break;
    case 'M': conf.c.mu = atof(optarg); break;
    case 'S': conf.mu_somatic = atof(optarg); break;
    case 'C': conf.c.contam = atof(optarg); break;
    case 'P': conf.c.prior1 = atof(optarg); break;
    case 'Q': conf.c.prior2 = atof(optarg); break;
    case 'H': conf.c.noheader = 1; break;
    case 'i': tumo_fn = optarg; break;
    case 'j': norm_fn = optarg; break;
    case 'r': reffn = optarg; break;
    case 'g': reg = optarg; break;
    case 'o': outfn = optarg; break;
    case 's': conf.c.step = atoi(optarg); break;
    case 'q': conf.c.n_threads = atoi(optarg); break;
    case 'b': conf.c.min_base_qual = atoi(optarg); break;
    case 't': conf.c.max_retention = atoi(optarg); break;
    case 'l': conf.c.min_read_len = atoi(optarg); break;
    case 'e': conf.c.min_dist_end = atoi(optarg); break;
    case 'c': conf.c.filter_secondary = 0; break;
    case 'u': conf.c.filter_duplicate = 0; break;
    case 'p': conf.c.filter_ppair = 0; break;
    case 'm': conf.c.min_mapq = atoi(optarg); break;
    case 'n': conf.c.max_nm = atoi(optarg); break;
    case 'v': conf.c.verbose = 1; break;
    case 'h': return usage();
    default:
      fprintf(stderr, "[%s:%d] Unrecognized command: %c.\n", __func__, __LINE__, c);
      exit(1);
      break;
    }
  }

  conf.c.prior0 = 1.0 - conf.c.prior1 - conf.c.prior2;

  if (!tumo_fn || !norm_fn || !reffn) {
    usage();
    exit(1);
  }

  wqueue_t(window) *wq = wqueue_init(window, 100000);
  pthread_t *processors = calloc(conf.c.n_threads, sizeof(pthread_t));
  result_t *results = calloc(conf.c.n_threads, sizeof(result_t));
  int i; unsigned j;
  samfile_t *tumo_bam = samopen(tumo_fn, "rb", 0);
  samfile_t *norm_bam = samopen(norm_fn, "rb", 0);

  /* sort sequence name by alphabetic order, chr1, chr10, chr11 ... */
  kstring_t header; header.l = header.m = 0; header.s = 0;
  kputs("##fileformat=VCFv4.1\n", &header);
  ksprintf(&header, "##reference=%s\n", reffn);
  ksprintf(&header, "##source=biscuitV%s\n", PACKAGE_VERSION);
  target_v *targets = init_target_v(50);
  target_t *t;
  for (i=0; i<tumo_bam->header->n_targets; ++i) {
    t = next_ref_target_v(targets);
    t->tid = i;
    t->name = tumo_bam->header->target_name[i];
    t->len = tumo_bam->header->target_len[i];
  }
  qsort(targets->buffer, targets->size, sizeof(target_t), compare_targets);
  for (j=0; j<targets->size; ++j) {
    t = ref_target_v(targets, j);
    ksprintf(&header, "##contig=<ID=%s,length=%d>\n", t->name, t->len);
  }
  kputs("##program=<cmd=biscuit", &header);
  for (i=0; i<argc; ++i)
    ksprintf(&header, " %s", argv[i]);
  kputs(">\n", &header);
  char plpclass[20], plpbsstrand[4];
  strcpy(plpclass, "tumor"); head_append_info(plpclass, 'T', &header);
  strcpy(plpclass, "normal"); head_append_info(plpclass, 'N', &header);
  
  if (conf.c.verbose) {
    strcpy(plpclass, "tumor"); strcpy(plpbsstrand, "BSW");
    head_append_verbose(plpclass, plpbsstrand, 'T', '0', &header);
    strcpy(plpclass, "tumor"); strcpy(plpbsstrand, "BSC");
    head_append_verbose(plpclass, plpbsstrand, 'T', '1', &header);
    strcpy(plpclass, "normal"); strcpy(plpbsstrand, "BSW");
    head_append_verbose(plpclass, plpbsstrand, 'N', '0', &header);
    strcpy(plpclass, "normal"); strcpy(plpbsstrand, "BSC");
    head_append_verbose(plpclass, plpbsstrand, 'N', '1', &header);
  }

  kputs("##FORMAT=<ID=GT,Number=1,Type=Integer,Description=\"Genotype from normal\">\n", &header);
  kputs("##FORMAT=<ID=GL,Number=G,Type=Integer,Description=\"Genotype likelihoods\">\n", &header);
  kputs("##FORMAT=<ID=GQ,Number=1,Type=Float,Description=\"Genotype quality (phred-scaled)\">\n", &header);
  kputs("##FORMAT=<ID=SST,Number=1,Type=Integer,Description=\"Somatic status of the variant. 1) somatic; 2) wildtype; 3) unknown;\">\n", &header);
  kputs("##FORMAT=<ID=SSC,Number=1,Type=Float,Description=\"Somatic score\">\n", &header);


  pthread_t writer;
  writer_conf_t writer_conf = {
    .q = wqueue_init(record, 100000),
    .outfn = outfn,
    .header = conf.c.noheader?0:header.s,
  };
  pthread_create(&writer, NULL, write_func, &writer_conf);
  for (i=0; i<conf.c.n_threads; ++i) {
    results[i].q = wq;
    results[i].rq = writer_conf.q;
    results[i].ref_fn = reffn;
    results[i].tumo_fn = tumo_fn;
    results[i].norm_fn = norm_fn;
    results[i].conf = &conf;
    pthread_create(&processors[i], NULL, process_func, &results[i]);
  }

  window_t w; memset(&w, 0, sizeof(window_t));
  uint32_t wbeg;
  int64_t block_id=0;
  if (reg) {
    int tid;
    uint32_t beg, end;
    bam_parse_region(tumo_bam->header, reg, &tid, (int*) &beg, (int*) &end);
    /* chromosome are assumed to be shorter than 2**29 */
    beg++; end++;
    if (beg<=0) beg = 1;
    if (end>tumo_bam->header->target_len[tid]) end = tumo_bam->header->target_len[tid];
    for (wbeg = beg; wbeg < end; wbeg += conf.c.step, block_id++) {
      w.tid = tid;
      w.block_id = block_id;
      w.beg = wbeg;
      w.end = wbeg + conf.c.step;
      if (w.end > end) w.end = end;
      wqueue_put(window, wq, &w);
    }
  } else {

    for (j=0; j<targets->size; ++j) {
      t = ref_target_v(targets, j);
      for (wbeg = 1; wbeg < t->len; wbeg += conf.c.step, block_id++) {
        w.tid = t->tid;
        w.block_id = block_id;
        w.beg = wbeg;
        w.end = wbeg+conf.c.step;
        if (w.end > t->len) w.end = t->len;
        wqueue_put(window, wq, &w);
      }
    }
  }
  for (i=0; i<conf.c.n_threads; ++i) {
    w.tid = -1;
    wqueue_put(window, wq, &w);
  }

  for (i=0; i<conf.c.n_threads; ++i) {
    pthread_join(processors[i], NULL);
  }
  record_t rec = { .block_id = RECORD_QUEUE_END };
  wqueue_put2(record, writer_conf.q, rec);
  pthread_join(writer, NULL);
  wqueue_destroy(record, writer_conf.q);
  free_target_v(targets);
  free(results);
  free(processors);
  free(header.s);

  wqueue_destroy(window, wq);
  samclose(tumo_bam);

  samclose(norm_bam);
  return 0;
}
