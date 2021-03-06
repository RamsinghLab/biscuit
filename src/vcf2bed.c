#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "wstr.h"

typedef struct conf_t { // {{{1
  char target[5];
  int mincov;
  int verbose;
} conf_t; // }}}1

typedef struct bed1_t { // {{{1
  char *chrm;
  int64_t pos;
  int64_t end;
  char ref;
  double beta;
  int cov;
} bed1_t; // }}}1

bed1_t *init_bed1() { // {{{1
  bed1_t *b=calloc(1, sizeof(bed1_t));
  b->chrm=0;
  return b;
} // }}}1

void free_bed1(bed1_t *b) { // {{{1
  free(b);
} // }}}1


// original version
static int vcf_parse1(char *line, bed1_t *b, int *is_cg, int *is_hcg, int *is_gch) { // {{{1

  char *tok;
  char *linerest=0, *fieldrest=0;

  *is_cg=0; *is_hcg=0; *is_gch=0;

  /* CHROM */
  tok=strtok_r(line, "\t", &linerest);
  b->chrm = realloc(b->chrm, strlen(tok)+1);
  strcpy(b->chrm, tok);

  /* POS */
  tok=strtok_r(NULL, "\t", &linerest);
  ensure_number(tok);
  b->pos = atoi(tok); b->end = atoi(tok);
  
  /* ID */
  tok=strtok_r(NULL, "\t", &linerest);

  /* REF */
  tok=strtok_r(NULL, "\t", &linerest);
  b->ref = tok[0];

  /* ALT */
  tok=strtok_r(NULL, "\t", &linerest);

  /* QUAL */
  tok=strtok_r(NULL, "\t", &linerest);

  /* PASS FILTER */
  tok=strtok_r(NULL, "\t", &linerest);

  /* INFO */
  tok=strtok_r(NULL, "\t", &linerest);
  int i;
  for (i=0; i<(signed)strlen(tok)-2; ++i) {
    if (strncmp(tok+i,"N5=",3)==0) {
      if (tok[i+4]=='G' && tok[i+5]=='C' && tok[i+6]!='G') *is_gch=1;
      if (tok[i+5]=='C' && tok[i+6]=='G') {
        *is_cg = 1;
        if (tok[i+4]!='G') *is_hcg=1;
      }
      break;
    }
  }

  /* not any of the target */
  if ((!*is_cg) && (!*is_hcg) && (!*is_gch)) return 0;

  /* FORMAT */
  tok=strtok_r(NULL, "\t", &linerest);
  int cv_index=-1, bt_index=-1;
  char *field;
  field=strtok_r(tok, ":", &fieldrest);
  i=0;
  while(field) {
    if (field[0]=='C' && field[1]=='V') {
      cv_index = i;
    }
    if (field[0]=='B' && field[1]=='T') {
      bt_index = i;
    }
    ++i;
    field=strtok_r(NULL, ":", &fieldrest);
  }

  /* no coverage or beta info */
  if (cv_index<0 || bt_index<0) return 0;

  /* FORMAT content */
  tok = strtok_r(NULL, "\t", &linerest);
  i=0;
  int coverage=-1; double beta=-1.0;
  field=strtok_r(tok, ":", &fieldrest);
  while (field) {
    if (i==cv_index) {
      ensure_number(field);
      coverage=atoi(field);
    }
    if (i==bt_index) {
      ensure_number(field);
      beta=atof(field);
    }
    ++i;
    field=strtok_r(NULL, ":", &fieldrest);
  }

  /* no coverage or beta info */
  if (coverage<0||beta<0) return 0;

  b->cov = coverage;
  b->beta = beta;

  return 1;
} // }}}1

void vcf2cg(gzFile FH, conf_t *conf) { // {{{1

  bed1_t *b=init_bed1();
  bed1_t *p=init_bed1();

  kstring_t str;
  str.s = 0; str.l = str.m = 0;

  int is_cg, is_hcg, is_gch;
  int p_valid=0;
  while (1) {
    int c=gzgetc(FH);
    if (c=='\n' || c==EOF) {
      int merged=0;
      if (str.l>2 && str.s[0] != '#' && strcount_char(str.s, '\t')>=8) {
        int parse_ok = vcf_parse1(str.s, b, &is_cg, &is_hcg, &is_gch);
        if (parse_ok && p_valid) {
          /* printf("%d\t%d\t%d\t%c\t%c\t%s\t%s\n", is_cg, p->pos, b->pos, p->ref, b->ref, p->chrm, b->chrm); */
          if (is_cg && p->pos+1 == b->pos && p->ref == 'C' && b->ref == 'G' && strcmp(p->chrm, b->chrm)==0) { /* merge CG */
            p->end++;
            /* fprintf(stdout, "merge%s\t%d\t%d\t%1.2f\t%d\n",b->chrm,b->pos,b->pos,b->beta,b->cov); */
            p->beta = (double)(p->beta*p->cov+b->beta*b->cov)/(p->cov+b->cov);
            p->cov += b->cov;
            merged=1;
          }
        }
        if (p_valid) {
          if (p->cov >= conf->mincov) {
            fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", p->chrm, p->pos-1, p->end, p->beta);
          }
          p_valid = 0;
        }

        if (!merged && parse_ok && is_cg) {
          /* fprintf(stdout, "%s\t%d\t%d\t%1.2f\t%d\n",b->chrm,b->pos,b->pos,b->beta,b->cov); */
          bed1_t *tmp;
          tmp = p; p = b; b = tmp;
          p_valid=1;
        }
      }
      str.l = 0;                /* clean line */
      if (c==EOF) break;
    } else {
      kputc(c, &str);
    }
  }
  if (p_valid) {
    if (p->cov >= conf->mincov) {
      fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", p->chrm, p->pos-1, p->end, p->beta);
    }
  }
} // }}}1

void vcf2hcg(gzFile FH, conf_t *conf) { // {{{1

  bed1_t *b=init_bed1();
  bed1_t *p=init_bed1();

  kstring_t str;
  str.s = 0; str.l = str.m = 0;

  int is_cg, is_hcg, is_gch;
  int p_valid=0;
  while (1) {
    int c=gzgetc(FH);
    if (c=='\n' || c==EOF) {
      int merged=0;
      if (str.l>2 && str.s[0] != '#' && strcount_char(str.s, '\t')>=8) {
        int parse_ok = vcf_parse1(str.s, b, &is_cg, &is_hcg, &is_gch);
        if (parse_ok && p_valid) {
          if (is_hcg && p->pos+1 == b->pos && p->ref == 'C' && b->ref == 'G' && strcmp(p->chrm, b->chrm)==0) { /* merge CG */
            p->end++;
            p->beta = (double)(p->beta*p->cov+b->beta*b->cov)/(p->cov+b->cov);
            p->cov += b->cov;
            merged=1;
          }
        }
        if (p_valid) {
          if (p->cov >= conf->mincov) {
            fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", p->chrm, p->pos-1, p->end, p->beta);
          }
          p_valid = 0;
        }

        if (!merged && parse_ok && is_hcg) {
          bed1_t *tmp;
          tmp = p; p = b; b = tmp;
          p_valid=1;
        }
      }
      str.l = 0;                /* clean line */
      if (c==EOF) break;
    } else {
      kputc(c, &str);
    }
  }
  if (p_valid) {
    if (p->cov >= conf->mincov) {
      fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", p->chrm, p->pos-1, p->end, p->beta);
    }
  }

} // }}}1

void vcf2gch(gzFile FH, conf_t *conf) { // {{{1

  bed1_t *b=init_bed1();
  kstring_t str;
  str.s = 0; str.l = str.m = 0;

  int is_cg, is_hcg, is_gch;
  while (1) {
    int c=gzgetc(FH);
    if (c=='\n' || c==EOF) {
      if (str.l>2 && str.s[0] != '#' && strcount_char(str.s, '\t')>=8) {
        int parse_ok = vcf_parse1(str.s, b, &is_cg, &is_hcg, &is_gch);
        if (parse_ok && is_gch && b->cov >= conf->mincov) {
          fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", b->chrm, b->pos-1, b->end, b->beta);
        }
      }
      str.l = 0;                /* clean line */
      if (c==EOF) break;
    } else {
      kputc(c, &str);
    }
  }
} // }}}1


// this is gross, sorry... although vcf_parse1 could call it internally... --t
static int vcf_parse0(char *line, bed1_t *b, int *is_c) { // {{{1

  char *tok;
  char *linerest=0, *fieldrest=0;

  *is_c=0;

  /* CHROM */
  tok=strtok_r(line, "\t", &linerest);
  b->chrm = realloc(b->chrm, strlen(tok)+1);
  strcpy(b->chrm, tok);

  /* POS */
  tok=strtok_r(NULL, "\t", &linerest);
  ensure_number(tok);
  b->pos = atoi(tok); b->end = atoi(tok);
  
  /* ID */
  tok=strtok_r(NULL, "\t", &linerest);

  /* REF */
  tok=strtok_r(NULL, "\t", &linerest);
  b->ref = tok[0];

  /* ALT */
  tok=strtok_r(NULL, "\t", &linerest);

  /* QUAL */
  tok=strtok_r(NULL, "\t", &linerest);

  /* PASS FILTER */
  tok=strtok_r(NULL, "\t", &linerest);

  /* INFO */
  tok=strtok_r(NULL, "\t", &linerest);
  int i;
  for (i=0; i<(signed)strlen(tok)-2; ++i) {
    if (strncmp(tok+i,"N5=",3)==0) {
      if (tok[i+5]=='C') *is_c=1;
      break;
    }
  }

  /* not any of the target */
  if (!*is_c) return 0;

  /* FORMAT */
  tok=strtok_r(NULL, "\t", &linerest);
  int cv_index=-1, bt_index=-1;
  char *field;
  field=strtok_r(tok, ":", &fieldrest);
  i=0;
  while(field) {
    if (field[0]=='C' && field[1]=='V') {
      cv_index = i;
    }
    if (field[0]=='B' && field[1]=='T') {
      bt_index = i;
    }
    ++i;
    field=strtok_r(NULL, ":", &fieldrest);
  }

  /* no coverage or beta info */
  if (cv_index<0 || bt_index<0) return 0;

  /* FORMAT content */
  tok = strtok_r(NULL, "\t", &linerest);
  i=0;
  int coverage=-1; double beta=-1.0;
  field=strtok_r(tok, ":", &fieldrest);
  while (field) {
    if (i==cv_index) {
      ensure_number(field);
      coverage=atoi(field);
    }
    if (i==bt_index) {
      ensure_number(field);
      beta=atof(field);
    }
    ++i;
    field=strtok_r(NULL, ":", &fieldrest);
  }

  /* no coverage or beta info */
  if (coverage<0||beta<0) return 0;

  b->cov = coverage;
  b->beta = beta;

  return 1;
} // }}}1

void vcf2c(gzFile FH, conf_t *conf) { // {{{1

  bed1_t *b=init_bed1();
  kstring_t str;
  str.s = 0; str.l = str.m = 0;
  int is_c;

  while (1) {
    int c=gzgetc(FH);
    if (c=='\n' || c==EOF) {
      if (str.l>2 && str.s[0] != '#' && strcount_char(str.s, '\t')>=8) {
        int parse_ok = vcf_parse0(str.s, b, &is_c); 
        if (parse_ok && is_c && b->cov >= conf->mincov) {
          fprintf(stdout, "%s\t%"PRId64"\t%"PRId64"\t%1.3f\n", b->chrm, b->pos-1, b->end, b->beta);
        }
      }
      str.l = 0;                /* clean line */
      if (c==EOF) break;
    } else {
      kputc(c, &str);
    }
  }
} // }}}1


int main_vcf2bed(int argc, char *argv[]) // {{{1
{ 
  conf_t conf = {.verbose=0, .mincov=3};
  strcpy(conf.target, "cg");

  int c;
  while ((c = getopt(argc, argv, "k:t:V:h")) >= 0) {
    switch (c) {
    case 'k': conf.mincov = atoi(optarg); break;
    case 't': {
      if (strcmp(optarg, "cg")  !=0 && 
          strcmp(optarg, "hcg") !=0 && 
          strcmp(optarg, "gch") !=0 && 
          strcmp(optarg, "c")   !=0   ) {
        fprintf(stderr, 
                "[%s:%d] Invalid option for -t: %s. \n", 
                __func__, __LINE__, optarg);
        fflush(stderr);
        exit(1);
      }
      strcpy(conf.target, optarg);
      break;
    }
    case 'V': conf.verbose = atoi(optarg); break;
    case 'h': {
      fprintf(stderr, "\n");
      fprintf(stderr, "Usage: biscuit vcf2bed [options] vcf \n");
      fprintf(stderr, "Input options:\n");
      fprintf(stderr, "     -t STRING extract type {cg, hcg, gch, c} [%s]\n", 
              conf.target);
      fprintf(stderr, "     -k INT    minimum coverage [%d]\n", conf.mincov);
      fprintf(stderr, "     -V INT    verbose level [%d].\n", conf.verbose);
      fprintf(stderr, "     -h        this help.\n");
      fprintf(stderr, "\n");
      return 1;
    }
    default:
      fprintf(stderr, "[%s:%d] Unrecognized command: %c.\n", 
              __func__, __LINE__, c);
      fflush(stderr);
      exit(1);
      break;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, 
            "[%s:%d] Please provide input vcf\n", 
            __func__, 
            __LINE__);
    fflush(stderr);
    exit(1);
  }
  char *vcf_fn = argv[optind];
  gzFile FH=gzopen(vcf_fn, "r");
  if (!FH) {
    fprintf(stderr, 
            "[%s:%d] Cannot open VCF file: %s\n", 
            __func__, 
            __LINE__, 
            vcf_fn);
    fflush(stderr);
    exit(1);
  }
  if (strcmp(conf.target, "cg")==0) vcf2cg(FH, &conf);
  if (strcmp(conf.target, "hcg")==0) vcf2hcg(FH, &conf);
  if (strcmp(conf.target, "gch")==0) vcf2gch(FH, &conf);
  if (strcmp(conf.target, "c")==0) vcf2c(FH, &conf);

  return 0;
} // }}}1
