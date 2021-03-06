/**
 * @file dctz-decomp-lib.c
 * @author Seung Woo Son
 * @date July 2019
 * @brief DCTZ decompression library routine
 * (C) 2019 University of Massachuetts Lowell.
       See LICENSE in top-level directory.
*/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <math.h>
#ifdef TIME_DEBUG
#include <sys/time.h> /* struct timeval */
#endif
#include "fftw3.h"
#include "zlib.h"
#include "dctz.h"
#include "dct.h"

int dctz_decompress (char *a_z, double *a_r)
{
  int N, i, j, nblk;
#ifdef TIME_DEBUG
  struct timeval start_t, end_t, gstart_t; /* for measuring timing */
  double sf_t, zlib_t, idct_t, decomp_t, malloc_t, genbin_t;
#endif
  double *a_xr, SF;
  double *bin_maxes, *bin_center;
  unsigned char *bin_index, *bin_indexz;
  unsigned int tot_AC_exact_count;
#ifdef USE_TRUNCATE
  float *DC, *DCz, *AC_exact;
#else
  double *DC, *DCz, *AC_exact;
#endif /* USE_TRUNCATE */
  struct header h;
  double error_bound;
#ifdef USE_QTABLE
  double *qtable;    // Quantizer Table
#endif /* USE_QTABLE */

  char *cur_p = a_z;
  memcpy (&h, a_z, sizeof(struct header));
  cur_p = a_z + sizeof(struct header);
  N = h.num_elements;
  error_bound = h.error_bound;
  nblk = ceil(N/BLK_SZ);
  tot_AC_exact_count = h.tot_AC_exact_count;
  SF = h.scaling_factor;
#ifdef USE_QTABLE
  int K = h.bindex_count;
  if (NULL == (bin_indexz = (unsigned char *)malloc (K*sizeof(unsigned char)))) {
    fprintf (stderr, "Out of memory: bin_indexz[]\n");
    exit (1);
  }
#else
  if (NULL == (bin_indexz = (unsigned char *)malloc (N*sizeof(unsigned char)))) {
    fprintf (stderr, "Out of memory: bin_indexz[]\n");
    exit (1);
  }

#endif  

#ifdef DEBUG
  printf ("nitems=%d, tot_AC_exact_count=%d, scaling_factor=%e, bindex_sz_compressed=%d, DC_sz_compressed=%d, AC_exact_sz_compressed=%d,\n", h.num_elements, h.tot_AC_exact_count, h.scaling_factor, h.bindex_sz_compressed, h.DC_sz_compressed, h.AC_exact_sz_compressed);
#endif
#ifdef DEBUG
  printf ("N=%d, nblk=%d, SF=%e\n", N, nblk, SF);
#endif


#ifdef USE_TRUNCATE
  if (NULL == (DCz = (float *)malloc (nblk*sizeof(float)))) {
    fprintf (stderr, "Out of memory: DCz[]\n");
    exit (1);
  }
#else
  if (NULL == (DCz = (double *)malloc (nblk*sizeof(double)))) {
    fprintf (stderr, "Out of memory: DCz[]\n");
    exit (1);
  }
#endif

#ifdef USE_TRUNCATE
  if (NULL == (AC_exact = (float *)malloc (tot_AC_exact_count*sizeof(float)))) {
    fprintf (stderr, "Out of memory: AC_exactz\n");
    exit (1);
  }
#else
  if (NULL == (AC_exact = (double *)malloc (tot_AC_exact_count*sizeof(double)))) {
    fprintf (stderr, "Out of memory: AC_exactz\n");
    exit (1);
  }
#endif

#ifdef USE_QTABLE
  // Initialize  Quantizer Table
  if (NULL == (qtable = (double *)malloc (BLK_SZ*sizeof(double)))) {
      fprintf (stderr, "Out of memory: qtable\n");
      exit (1);
   }
#endif

  if (NULL == (a_xr = (double *)malloc (N*sizeof(double)))) {
    fprintf (stderr, "Out of memory: a_xr\n");
    exit (1);
  }

  memcpy (bin_indexz, cur_p, h.bindex_sz_compressed);
  cur_p += h.bindex_sz_compressed;
//  memcpy (AC_exact_countz, cur_p, h.AC_exact_count_sz_compressed);
//  cur_p += h.AC_exact_count_sz_compressed;
  memcpy (DCz, cur_p, h.DC_sz_compressed);
  cur_p += h.DC_sz_compressed;
  memcpy (AC_exact, cur_p, h.AC_exact_sz_compressed);
#ifdef USE_QTABLE
  cur_p += h.AC_exact_sz_compressed;
  memcpy (qtable, cur_p, BLK_SZ*sizeof(double));
#endif

#ifdef USE_QTABLE
#ifdef DEBUG
    printf ("Quantizer Table:\n");
    for (j=1; j<BLK_SZ ; j++){ // Show Quantizer Table
      printf ("qtable[%d] = %e \n", j, qtable[j]);
    }
#endif
#endif

#ifdef TIME_DEBUG
  gettimeofday (&start_t, NULL);
  gstart_t = start_t;
#endif

#ifdef USE_QTABLE
  uLong ucompSize_binindex = K*sizeof(unsigned char);
  if (NULL == (bin_index = (unsigned char *)malloc (K*sizeof(unsigned char)))) {
    fprintf (stderr, "Out of memory: bin_index[]\n");
    exit (1);
  }
#else  
  uLong ucompSize_binindex = N*sizeof(unsigned char);
  if (NULL == (bin_index = (unsigned char *)malloc (N*sizeof(unsigned char)))) {
    fprintf (stderr, "Out of memory: bin_index[]\n");
    exit (1);
  }
#endif  
  uLong compSize_binindex = compressBound (ucompSize_binindex);

  uLong ucompSize_DC = nblk*sizeof(float);
  uLong compSize_DC = compressBound (ucompSize_DC);

  //uLong ucompSize_AC_exact = tot_AC_exact_count*sizeof(float);

  /* setup for decompress */
  z_stream infstream[2];
  infstream[0].zalloc = Z_NULL;
  infstream[0].zfree = Z_NULL;
  infstream[0].opaque = Z_NULL;

  inflateInit (&infstream[0]);

  /* decompress bin_index */
  infstream[0].avail_in = compSize_binindex;
  infstream[0].next_in = (Bytef *)bin_indexz;
  infstream[0].avail_out = ucompSize_binindex;
  infstream[0].next_out = (Bytef *)bin_index;

  inflate (&infstream[0], Z_NO_FLUSH);
  inflateEnd (&infstream[0]);
#ifndef DEBUG
  printf ("uncompressed bin_index size is: %lu\n", infstream[0].total_out);
#endif

  /* decompress DC */
  infstream[1].zalloc = Z_NULL;
  infstream[1].zfree = Z_NULL;
  infstream[1].opaque = Z_NULL;

#ifdef USE_TRUNCATE
  if (NULL == (DC = (float *)malloc (nblk*sizeof(float)))) {
    fprintf (stderr, "Out of memory: DC[]\n");
    exit (1);
  }
#else
  if (NULL == (DC = (double *)malloc (nblk*sizeof(double)))) {
    fprintf (stderr, "Out of memory: DC[]\n");
    exit (1);
  }
#endif

  inflateInit (&infstream[1]);

  infstream[1].avail_in = compSize_DC;
  infstream[1].next_in = (Bytef *)DCz;
  infstream[1].avail_out = ucompSize_DC;
  infstream[1].next_out = (Bytef *)DC;

  inflate (&infstream[1], Z_NO_FLUSH);
  inflateEnd (&infstream[1]);
#ifndef DEBUG
  printf ("uncompressed DC size is: %lu\n", infstream[1].total_out);
#endif

  free (bin_indexz);
  free (DCz);

#ifdef TIME_DEBUG
  gettimeofday (&end_t, NULL);
  zlib_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));

  gettimeofday (&start_t, NULL);
#endif
  
  /* restore AC_exact */
  nblk = ceil(h.num_elements/BLK_SZ);
 
  if (NULL == (bin_maxes = (double *)malloc (NBINS*sizeof(double)))) {
    fprintf (stderr, "Out of memory: bin_maxes\n");
    exit (1);
  }

  if (NULL == (bin_center = (double *)malloc (NBINS*sizeof(double)))) {
    fprintf (stderr, "Out of memory: bin_center\n");
    exit (1);
  }

#ifdef TIME_DEBUG
  gettimeofday (&end_t, NULL);
  malloc_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));

  gettimeofday (&start_t, NULL);
#endif
  
  gen_bins (0, 0, bin_maxes, bin_center, NBINS, error_bound);

#ifdef TIME_DEBUG
  gettimeofday (&end_t, NULL);
  genbin_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));

  gettimeofday (&start_t, NULL);
#endif
  
  unsigned int pos = 0;
  unsigned int c = N;
  
  /* IDCT block decomposed */
  for (i=0; i<nblk; i++) { // for each decomposed blk
    a_xr[i*BLK_SZ] = DC[i]; /* if USE_TRUNCATE, then float -> double */
#ifdef DEBUG
    printf ("a_xr[%d]=%e\n", i*BLK_SZ, a_xr[i*BLK_SZ]);
#endif
    for (j=1; j<BLK_SZ; j++) {
#ifdef USE_QTABLE
      unsigned char sbin_id;
#endif
      if (bin_index[i*BLK_SZ+j] == 255) {
#ifdef USE_QTABLE /* DCT_QT */
	sbin_id = bin_index[c++];
	if (sbin_id == 255) {
	  a_xr[i*BLK_SZ+j] = AC_exact[pos]; /* if USE_TRUNCATE, then float -> double */
	  pos++;
	}
	else {
	  a_xr[i*BLK_SZ+j] = bin_center[sbin_id];
	}
	a_xr[i*BLK_SZ+j] = a_xr[i*BLK_SZ+j] * qtable[j];	 
#else /* DCT_EC */
	a_xr[i*BLK_SZ+j] = AC_exact[pos]; /* if USE_TRUNCATE, then float -> double */
	pos++;
#endif /* USE_QTABLE */
      }
      else {
	a_xr[i*BLK_SZ+j] = bin_center[bin_index[i*BLK_SZ+j]];
      }
#ifdef DEBUG
      printf ("after a_xr[%d]=%e\n", i*BLK_SZ+j, a_xr[i*BLK_SZ]+j);
#endif
    }
    
    ifft_idct (BLK_SZ, a_xr+i*BLK_SZ, a_r+i*BLK_SZ);

#ifdef DEBUG
    printf ("block %d: after IDCT:\n", i);
    for (j=0; j<BLK_SZ && (i < 3); j++){ // show the first block results
      printf ("a_r[%d] = %e \n", i*BLK_SZ+j, a_r[i*BLK_SZ+j]);
    }
#endif
  }
  
  free (DC);

#ifdef TIME_DEBUG
  gettimeofday (&end_t, NULL);
  idct_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));
  
  gettimeofday (&start_t, NULL);
#endif
  
  double xscale = pow(10, SF-1);
  // deapply scaling factor
  if (SF != 1.0)
    for (i=0; i<N; i++)
      a_r[i] *= xscale;

#ifdef TIME_DEBUG
  gettimeofday (&end_t, NULL);
  sf_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));

  gettimeofday (&end_t, NULL);

  double decomp_rate;

  zlib_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(start_t.tv_sec*1000000 + start_t.tv_usec));
  decomp_t = (double)((end_t.tv_sec*1000000 + end_t.tv_usec)-(gstart_t.tv_sec*1000000 + gstart_t.tv_usec));
  decomp_rate = (N*sizeof(double)/(double)(1024*1024))/(decomp_t/1000000);

  printf ("sf_t=%f(s), idct_t=%f(s), zlib_t(uncompress)=%f(s)\n", sf_t/1000000, idct_t/1000000, zlib_t/1000000);
  printf ("malloc_t=%f(s), genbin=%f(s)\n", malloc_t/1000000, genbin_t/1000000);
  printf ("decomp_time = %f (s), decompression rate = %f (MB/s)\n", decomp_t/1000000, decomp_rate);
#endif

  idct_finish ();
  free (a_xr);
  free (AC_exact);
  free (bin_index);
  free (bin_maxes);
  free (bin_center);

  return (1);
}
