/**
 * bamCheckBadJoins
 *  Author: John St. John
 *  Date: 2/8/2012
 *
 *  This program looks for examples of weird mapping
 *  properties of a mate-pair file. Specifically it
 *  looks for cases where an inner scaffold mate strongly
 *  supports a join to a different genomic fragment.
 *
 */



/**
 * Includes
 *
 */

#include "common.h"
#include "options.h"
#include "linefile.h"
#include "bamFile.h"
#include "sam.h"



/**
 * Global Variables and definitions
 *
 */ 

#define DEFAULT_EDGES 4000
#define DEFAULT_MQ 30
#define MAX_OCOUNT_LEN 301
#define DEFAULT_MIN_INSERT 10
#define DEFAULT_MAX_INSERT 5000
#define DEFAULT_AVG_INSERT 1650
#define MAX_COUNT (1000)


/**
 * Command line options
 */ 

void usage()
  /* Explain usage and exit. */
{
  errAbort(
      "bamCheckBadJoins2 -- make list of per-base regions of unexpected mate-pair insert sizes.\n"
      "Usage: bamCheckBadJoins2 [options] bamFile.sorted.bam \n"
      "\noptions:\n"
      "\t-avgInsert=INT\tlibrary has this approximate average insert size(default: 1650)\n"
      "\t-minInsert=INT\tread pairs with inserts less than this value are marked as bad (default: 100)\n"
      "\t-maxInsert=INT\tread pairs with inserts greater than this value are marked as bad (default: 5000)\n"
      "\t-edges=INT\tskip this many bases at beginning and end of scaffolds (default: 4000)\n"
      "\t-minq=INT\tonly consider alignments where the left read has at least a mapq of (default: 30)\n"
      "\t-verbose\twrite some program status information to stderr.\n"
      "\t-help\twrite this help to the screen.\n"
      );
}//end usage()


static struct optionSpec options[] = {
  /* Structure holding command line options */
  {"minInsert",OPTION_INT},
  {"maxInsert",OPTION_INT},
  {"avgInsert",OPTION_INT},
  {"edges",OPTION_INT},
  {"minq",OPTION_INT},
  {"help",OPTION_BOOLEAN},
  {"verbose",OPTION_BOOLEAN},
  {NULL, 0}
}; //end options()

inline unsigned short mysumfunc(unsigned short *lst, int start, int end){
  unsigned short out = 0;
  int i;
  for(i=start;i<end;i++){
    out += lst[i];
  }
  return(out);
}

inline void makeWindows(int winlen, int len, unsigned short *point_counts, unsigned short *window_counts){
  int i;
  winlen = min(len,winlen);
  int winsum = min(MAX_COUNT,mysumfunc(point_counts,0,winlen));
  window_counts[winlen/2] = winsum;
  for(i=0;i<winlen/2;i++)
    window_counts[i] = winsum;
  for(i=(winlen/2)+1;i<len-(winlen/2);i++){
    window_counts[i] = min(window_counts[i-1] + point_counts[i+(winlen/2)] - point_counts[i-(winlen/2)-1], MAX_COUNT);
  }
  for(i=len-(winlen/2);i<len;i++)
    window_counts[i] = window_counts[i-1];
}



/**
 * Program's functions
 *
 */


void bamPrintInfo(samfile_t *bamFile, FILE* out, int edges, int avgInsert, int minInsert, int maxInsert, int minmq, boolean verbose)
  /* iterate through bam alignments, storing */
{
  int lastTID=-1; //real TIDs are never negative
  bam_header_t *header = bamFile->header;
  unsigned short *bad_range_insert_counts = NULL;
  unsigned short *discontiguous_insert_counts = NULL; //other or unmapped
  unsigned short *ok_insert_counts = NULL;
  unsigned short *window_discontiguous_insert_counts = NULL;
  boolean skipTID = TRUE;
  int length = 0;
  int i;
  int alnlen;

  bam1_t *b = bam_init1();
  fprintf(out, "#seq_name\tposition(0-based)\tisize_out_of_range\tdiscontiguous_in_avg_insert_window\tok_looking_inserts\n");
  while(samread(bamFile, b)>=0){
    if(b->core.tid != lastTID){
      //we have an alignment to something
      //new
      if (bad_range_insert_counts != NULL && discontiguous_insert_counts != NULL && ok_insert_counts != NULL){
        char *name = header->target_name[lastTID];

        makeWindows(avgInsert, length, discontiguous_insert_counts, window_discontiguous_insert_counts);

        for(i=edges;i<length-edges;i++)
          fprintf(out, "%s\t%d\t%hu\t%hu\t%hu\n", name, i, bad_range_insert_counts[i], window_discontiguous_insert_counts[i], ok_insert_counts[i] );



        //free old count structures
        free(bad_range_insert_counts);
        bad_range_insert_counts = NULL;
        free(discontiguous_insert_counts);
        discontiguous_insert_counts = NULL;
        free(ok_insert_counts);
        ok_insert_counts = NULL;
        free(window_discontiguous_insert_counts);
        window_discontiguous_insert_counts = NULL;

      }

      lastTID = b->core.tid;

      length = header->target_len[lastTID];

      //nothing to see here
      if(length <= min((2 * edges),avgInsert)){
        skipTID = TRUE;
        continue;
      }

      skipTID = FALSE;

      //calloc should 0 out arrays
      bad_range_insert_counts = (unsigned short *) calloc(length, sizeof(unsigned short));
      discontiguous_insert_counts = (unsigned short *) calloc(length, sizeof(unsigned short));
      ok_insert_counts = (unsigned short *) calloc(length, sizeof(unsigned short));
      window_discontiguous_insert_counts = (unsigned short *) calloc(length, sizeof(unsigned short));
   
    }
    if(skipTID == TRUE)
      continue;


    //now increment counts of our 
    if(b->core.qual >= minmq)
    { //this read aligns somewhere
      //deal with things
      //case 1: the mate doesn't align or the mate aligns to a different chromosome
      if((b->core.flag & BAM_FMUNMAP) || ((!(b->core.flag & BAM_FMUNMAP)) && b->core.tid != b->core.mtid && b->core.mtid != -1)){
        discontiguous_insert_counts[b->core.pos]++;
      }
      //case 2: the mate aligns to the same chromosome, and we only consider one read
      else if ((!(b->core.flag & BAM_FMUNMAP)) && (b->core.tid == b->core.mtid) && (b->core.flag & BAM_FREAD1)){
        int absIsize = abs(b->core.isize);
        int start = min(b->core.pos, b->core.mpos);
        int end = max(b->core.pos, b->core.mpos);
        //case 2a: the mate aligns outside of the expected range
        if((absIsize < minInsert) || (absIsize > maxInsert)){
          for(i = start; i <= end; i++)
            if(bad_range_insert_counts[i]<MAX_COUNT)
              bad_range_insert_counts[i]++;
        }
        //case 2b: the mate aligns nicely within the expected range
        else{
          for(i = start; i <= end; i++)
            if(ok_insert_counts[i]<MAX_COUNT)
              ok_insert_counts[i]++;
        }
      }
      


    }//end bucket incrementation


  }//end loop over reads

  
  //now that loop is done, take care of last bucket if it is there.
  if (bad_range_insert_counts != NULL && discontiguous_insert_counts != NULL && ok_insert_counts != NULL){
    char *name = header->target_name[lastTID];

    makeWindows(avgInsert, length, discontiguous_insert_counts, window_discontiguous_insert_counts);

    for(i=edges;i<length-edges;i++)
      fprintf(out, "%s\t%d\t%hu\t%hu\t%hu\n", name, i, bad_range_insert_counts[i], window_discontiguous_insert_counts[i], ok_insert_counts[i] );



    //free old count structures
    free(bad_range_insert_counts);
    bad_range_insert_counts = NULL;
    free(discontiguous_insert_counts);
    discontiguous_insert_counts = NULL;
    free(ok_insert_counts);
    ok_insert_counts = NULL;
    free(window_discontiguous_insert_counts);
    window_discontiguous_insert_counts = NULL; 

  }



  bam_destroy1(b);

}







int main(int argc, char *argv[])
  /* Process command line. */
{

  optionInit(&argc, argv, options);
  boolean help = optionExists("help");
  boolean verbose = optionExists("verbose");
  if(help || argc != 2) usage();
  if(!bamFileExists(argv[1])){
    fprintf(stderr,"ERROR: Bam file %s could not be found, or bam index not found.\n",argv[1]);
    usage();
  }

  int maxInsert = optionInt("maxInsert",DEFAULT_MAX_INSERT);
  int minInsert = optionInt("minInsert",DEFAULT_MIN_INSERT);
  int edgelen = optionInt("edges",DEFAULT_EDGES);
  int avgInsert = optionInt("avgInsert",DEFAULT_AVG_INSERT);
  int minq = optionInt("minq",DEFAULT_MQ);

  char *bamFileName;
  samfile_t *bamFile = bamOpen(argv[1],&bamFileName);

  bamPrintInfo(bamFile, stdout, edgelen, avgInsert, minInsert, maxInsert, minq, verbose);

  bamClose(&bamFile);


  return 0;
} //end main()


