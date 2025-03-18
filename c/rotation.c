
/*
 * Sivan Toledo, 2024.
 *
 * Example program for UltimateKalman in C.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#ifdef _WIN32
// for "unused" attribute
#define __attribute__(x)
#include <float.h>
// string.h for memcpy
#include <string.h>
#else
#include <unistd.h>
#endif

#include <math.h>

#include "kalman.h"
#include "parallel.h"

#include "cmdline_args.h"

double PI = 3.141592653589793;

/*
 * Matrices of Gaussian random numbers generated by MATLAB so as to produce the same simulations and estimates as the MATLAB version.
 *
 * To generate the random numbers in Matlab, use
 * rng(5); for j=2:16; evolErrs(1:2,j-1) = randn(2,1); end; for j=1:16; obsErrs(1:2,j) = randn(2,1); end; disp(evolErrs); disp(obsErrs);
 */

double evolErrs_rowwise[] = {
-0.343003152130103,-0.766711794483284,-0.016814112314737, 0.684339759945504,-1.401783282955619,-1.521660304521858,-0.127785244107286, 0.602860572524585,-0.139677982915557, 0.407768714902350, 0.397539533883833,-0.317539749169638,-0.779285825610984,-1.935513755513929, 0.678730596165904,
1.666349045016822, 2.635481573310387, 0.304155468427342, 0.055808274805755,-1.360112379179931, 1.054743814037827,-1.410338023439304,-0.456929290517258,-0.983310072206319, 0.242994841538368,-0.175692485792199,-1.101615186229668,-1.762205119649466, 1.526915548584107,-2.277161011565906
};

double obsErrs_rowwise[] = {
-1.428567988496096, 0.913205695955837,-1.576872295738796,-1.888336147279610, 1.116853507009928, 1.615888145666843,-0.102585012191329,-0.192732954692481, 0.160906008337421,-0.024849020282298,-1.001561909251739,-0.314462113181954,0.276865687293751, 0.175430340572582, 0.746792737753047, 1.648965874319728,
-1.114618464565160, 0.976371425014641, 0.204080086636545, 0.736193913185726, 0.743379272133998,-1.666530392059792, 0.622727541956653, 0.794595441386172, 0.539084689771962,-2.548385761079745,-1.161623730001803, 1.066876935479899,1.748562141782206, 0.362976707912966, 0.842263598054067, 1.725578381396231
};

/*
 * C(i:i+m,j:j+n) += A(p:p+m,q:q+w) * B(k:k+w,l:l+n)
 */

void matrix_mutate_multiply_accumulate(
				kalman_matrix_t* C, int i, int j,
				kalman_matrix_t* A, int p, int q,
				kalman_matrix_t* B, int k, int l,
				int Csub_rows, int Csub_cols, int Asub_cols
				) {
  int r,c,s;

  for (r=0; r<Csub_rows; r++) {
    for (c=0; c<Csub_cols; c++) {
      double x = matrix_get(C, i+r, j+c);
      for (s=0; s<Asub_cols; s++) {
    	  matrix_get(A,r+p,s+q);
    	  matrix_get(B, k+s, l+c);
    	  x += matrix_get(A,r+p,s+q) * matrix_get(B, k+s, l+c);
      }
      matrix_set(C, i+r, j+c, x);
    }
  }
}

/*
 * C(i:i+m,j:j+n) += alpha * B(k:k+m,l:l+n)
 */

void matrix_mutate_scale_accumulate(
				kalman_matrix_t* C, int i, int j,
				double alpha,
				kalman_matrix_t* B, int k, int l,
				int rows, int cols
				) {
  int r,c;

  for (r=0; r<rows; r++) {
    for (c=0; c<cols; c++) {
    	double x = matrix_get(C, i+r, j+c);
        double y = matrix_get(B, k+r, l+c);
        matrix_set(C, i+r, j+c, x + alpha * y);
    }
  }
}

static int streq(char* constant, char* value) {
  int l = strlen(constant);
  if (strncmp(constant,value,l)==0 && strlen(value)==l) {
    printf("streq %s == %s => %d\n",constant,value,1);
    return 1;
  }
  printf("streq %s == %s => %d\n",constant,value,0);
  return 0;
}

int main(int argc, char* argv[]) {


  //int n, k;
  int nthreads, blocksize;
  char *algorithm;
  int present;

  parse_args(argc, argv);
  //present = get_int_param   ("n",         &n, 6);
  //present = get_int_param   ("k",         &k, 100000);
  present = get_string_param("algorithm", &algorithm, "ultimate");
  present = get_int_param   ("nthreads",  &nthreads,  -1);
  present = get_int_param   ("blocksize", &blocksize, -1);
  check_unused_args();

  //printf("rotation n=%d k=%d algorithm=%d nthreads=%d blocksize=%d (-1 means do not set)\n");
  printf("rotation algorithm=%s nthreads=%d blocksize=%d (-1 means do not set)\n",algorithm,nthreads,blocksize);

  kalman_options_t options = KALMAN_ALGORITHM_ULTIMATE;
  if (streq("ultimate",    algorithm)) options = KALMAN_ALGORITHM_ULTIMATE;
  if (streq("conventional",algorithm)) options = KALMAN_ALGORITHM_CONVENTIONAL;
  if (streq("oddeven",     algorithm)) options = KALMAN_ALGORITHM_ODDEVEN;
  if (streq("associative", algorithm)) options = KALMAN_ALGORITHM_ASSOCIATIVE;

  printf("results should be identical to those produced by rotation(UltimateKalman,5,2) in MATLAB\n");
	
  if (nthreads != -1)  parallel_set_thread_limit(nthreads);
  if (blocksize != -1) parallel_set_blocksize(blocksize);

	int i;
	
	double alpha = 2.0 * PI / 16.0;

	double F_rowwise[] = {
			cos(alpha), -sin(alpha),
			sin(alpha),  cos(alpha),
	};

	double G_rowwise[] = {
	  1, 0,
	  0, 1,
	  1, 1,
	  2, 1,
	  1, 2,
	  3, 1
	};

	double evolutionStd   = 1e-3;
	double observationStd = 1e-1;

	int k = 16;

	int obs_dim = 2;

	kalman_matrix_t* evolErrs = matrix_create_from_rowwise(evolErrs_rowwise, 2, 15);
	kalman_matrix_t* obsErrs  = matrix_create_from_rowwise(obsErrs_rowwise , 2, 16);

	kalman_matrix_t* H = matrix_create_identity(2, 2);
	kalman_matrix_t* F = matrix_create_from_rowwise(F_rowwise, 2, 2);
	kalman_matrix_t* G = matrix_create_from_rowwise(G_rowwise, 6, 2);
	
	printf("F = ");
	matrix_print(F, "%.4f");

	G = matrix_create_sub(G,0,obs_dim,0,2);

	printf("G = ");
	matrix_print(G, "%.4f");

	char K_type = 'W';
	kalman_matrix_t* K = matrix_create_constant(2,2,0.0);
	for (i=0; i<2; i++) matrix_set(K, i,i, 1.0 / evolutionStd);
	
	printf("K = ");
	matrix_print(K, "%.4e");

	char C_type = 'W';
	kalman_matrix_t* C = matrix_create_constant(obs_dim,obs_dim,0.0);
	for (i=0; i<obs_dim; i++) matrix_set(C, i,i, 1.0 / observationStd);
	
	printf("C = ");
	matrix_print(C, "%.4e");

	kalman_matrix_t* states = matrix_create_constant(2,      k, 0.0);
	kalman_matrix_t* obs    = matrix_create_constant(obs_dim,k, 0.0);

	matrix_set(states, 0, 0, 1.0);
	matrix_set(states, 1, 0, 0.0);

	for (i=1; i<k; i++) {
	  matrix_mutate_multiply_accumulate(states,   0, i,
				     F,        0, 0,
				     states,   0, i-1,
				     2, 1, matrix_cols(F));

     matrix_mutate_scale_accumulate(states,     0, i,
				     evolutionStd,
				     evolErrs,   0, i-1,
				     2, 1);
	}

	printf("states = ");
	matrix_print(states, "%.4f");

	for (i=0; i<k; i++) {
	  matrix_mutate_multiply_accumulate(obs,      0, i,
				     G,        0, 0,
				     states,   0, i,
				     2, 1, matrix_cols(G));

	  matrix_mutate_scale_accumulate(obs,     0, i,
				     observationStd,
				     obsErrs,    0, i,
				     2, 1);
	}

	printf("obs = ");
	matrix_print(obs, "%.4f");
	
	kalman_matrix_t* zero = matrix_create_constant(2,1,0.0);

	kalman_matrix_t* o; // a single observation vector
	kalman_matrix_t* e; // a single estimate

	kalman_matrix_t* predicted = matrix_create_constant(2,k,0.0);
	kalman_matrix_t* filtered  = matrix_create_constant(2,k,0.0);
	kalman_matrix_t* smoothed  = matrix_create_constant(2,k,0.0);
	kalman_matrix_t* empty     = matrix_create_constant(0,0,0.0); // an empty matrix

	/*************************************************************/
	/* predict all the states from the first observation         */
	/*************************************************************/

	kalman_t* kalman = kalman_create_options(options);
	
	// first step
	printf("evolve-observe step %d\n",0);
	kalman_evolve(kalman, 2, NULL, NULL, NULL, NULL, K_type);

	o = matrix_create_sub(obs, 0, obs_dim, 0, 1);
	kalman_observe(kalman, G, o, C, C_type);
	matrix_free(o);

	e = kalman_estimate(kalman,0);
	matrix_mutate_copy_sub(predicted, 0, 0, e);
	matrix_free(e);

	printf("earliest->latest %d->%d\n",(int) kalman_earliest(kalman),(int) kalman_latest(kalman));

	// next steps (empty observations)
	for (i=1; i<k; i++) {
	  printf("prediction step %d\n",i);
	  kalman_evolve(kalman, 2, H, F, zero, K, K_type);

	  kalman_observe(kalman, NULL, NULL, NULL, C_type);

	  e = kalman_estimate(kalman,i);
	  matrix_mutate_copy_sub(predicted, 0, i, e);
	  matrix_free(e);
	}
	printf("earliest->latest %d->%d\n",(int) kalman_earliest(kalman),(int) kalman_latest(kalman));
	  
	/*************************************************************/
	/* roll back to the second state and compute filtered states */
	/*************************************************************/
	
	kalman_rollback(kalman,1);
	printf("earliest->latest %d->%d\n",(int) kalman_earliest(kalman),(int) kalman_latest(kalman));

	o = matrix_create_sub(obs, 0, obs_dim, 1, 1);
	kalman_observe(kalman, G, o, C, C_type);
	matrix_free(o);

	kalman_estimate(kalman,0);
	e = kalman_estimate(kalman,0);
	matrix_mutate_copy_sub(filtered, 0, 0, e);
	matrix_free(e);

	e = kalman_estimate(kalman,1);
	matrix_mutate_copy_sub(filtered, 0, 1, e);
	matrix_free(e);

	printf("earliest->latest %d->%d\n",(int) kalman_earliest(kalman),(int) kalman_latest(kalman));

	for (i=2; i<k; i++) {
		kalman_evolve(kalman, 2, H, F, zero, K, K_type);
		o = matrix_create_sub(obs, 0, obs_dim, i, 1);
		kalman_observe(kalman, G, o, C, C_type);
		matrix_free(o);

		e = kalman_estimate(kalman,i);
		matrix_mutate_copy_sub(filtered, 0, i, e);
		matrix_free(e);
	}

	/*************************************************************/
	/* smoothing                                                 */
	/*************************************************************/


	kalman_smooth(kalman);

	for (i=0; i<k; i++) {
		e = kalman_estimate(kalman,i);
		matrix_mutate_copy_sub(smoothed, 0, i, e);
		matrix_free(e);
	}

	kalman_matrix_t* W = kalman_covariance(kalman,0);
	char             t = kalman_covariance_type(kalman,0);
    //printf("covariance of smoothed estimate of state 0 (type %c) = ",t);
    //matrix_print(W, "%.2e");
    printf("covariance of smoothed estimate of state 0 = ");
    matrix_print(kalman_covariance_matrix_explicit(W,t), "%.2e");

	/*************************************************************/
	/* release kalman object                                     */
	/*************************************************************/

	kalman_free(kalman);

	/*************************************************************/
	/* print results out                                         */
	/*************************************************************/

	printf("predicted = ");
	matrix_print(predicted, "%.4f");

	printf("filtered = ");
	matrix_print(filtered,  "%.4f");

	printf("smoothed = ");
	matrix_print(smoothed,  "%.4f");

	printf("rotation done\n");
	return 0;
}

     
